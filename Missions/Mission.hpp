#pragma once
#include "base64/base64.h"
#include "llm/llm_query.hpp"
#include "pugixml/pugixml.hpp"
#include "tools/tools.hpp"
#include <chrono>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             std::string *out) {
  out->append((char *)contents, size * nmemb);
  return size * nmemb;
}

inline std::string serialize_node(pugi::xml_node node) {
  std::ostringstream ss;
  node.print(ss, "", pugi::format_raw);
  return ss.str();
}

inline std::string query_anthropic(const std::string &api_key,
                                   const std::string &prompt,
                                   const std::string &image_b64 = "") {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";
  std::string response;

  nlohmann::json content;
  if (!image_b64.empty()) {
    content = {{{"type", "image"},
                {"source",
                 {{"type", "base64"},
                  {"media_type", "image/png"},
                  {"data", image_b64}}}},
               {{"type", "text"}, {"text", prompt}}};
  } else {
    content = prompt;
  }

  nlohmann::json body = {
      {"model", "claude-sonnet-4-6"},
      {"max_tokens", 10000},
      {"stop_sequences", {"</tool>"}},
      {"messages", {{{"role", "user"}, {"content", content}}}}};

  std::string body_str = body.dump();

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
  headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

  curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  try {
    auto j = nlohmann::json::parse(response);
    return j["content"][0]["text"].get<std::string>();
  } catch (...) {
    return "";
  }
}

inline bool is_tool_call(const std::string &reply, nlohmann::json &out) {
  size_t i = 0;
  while (i < reply.size()) {
    size_t start = reply.find('{', i);
    if (start == std::string::npos)
      break;

    size_t pos = start;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    while (pos < reply.size()) {
      char c = reply[pos];
      if (in_string) {
        if (escaped)
          escaped = false;
        else if (c == '\\')
          escaped = true;
        else if (c == '"')
          in_string = false;
      } else {
        if (c == '"')
          in_string = true;
        else if (c == '{')
          depth++;
        else if (c == '}') {
          depth--;
          if (depth == 0) {
            std::string candidate = reply.substr(start, pos - start + 1);
            try {
              nlohmann::json j = nlohmann::json::parse(candidate);
              if (j.contains("tool")) {
                out = std::move(j);
                return true;
              }
            } catch (...) {
            }
            i = pos + 1;
            break;
          }
        }
      }
      pos++;
    }
    if (depth != 0)
      i = start + 1;
  }
  return false;
}

inline std::string construct_prompt(const std::string &mission,
                                    int iteration_id,
                                    const std::string &current_state,
                                    const std::string &notifications,
                                    const std::string &tool_instructions) {
  std::ostringstream p;

  // --- IDENTITY ---
  p << "You are an Agent executing a mission across multiple iterations.\n"
    << "Each iteration may contain multiple tool calls. After each iteration "
    << "you sleep for a set interval, then resume with updated state.\n\n";

  // --- CONTEXT ---
  p << "Mission: " << mission << "\n";
  p << "Iteration: " << iteration_id << "\n";
  p << "State: " << current_state << "\n";
  p << "Notifications: " << notifications << "\n\n";

  // --- TOOLS ---
  p << "TOOL USAGE\n" << tool_instructions << "\n\n";

  // --- OUTPUT FORMAT ---
  p << "OUTPUT FORMAT\n"
    << "Always begin your response with a <thinking> tag containing your "
       "reasoning.\n"
    << "Then output exactly one of the following:\n\n";

  p << "1. A tool call — see TOOL USAGE for format.\n\n";

  p << "2. End of iteration (when you have enough to conclude this "
       "iteration):\n"
    << "<last_cycle>\n"
    << "  <state>Where you are in the mission. What was accomplished, what "
       "remains, "
    << "and any variables needed for the next iteration. Be specific.</state>\n"
    << "  <interval>Milliseconds until next iteration. 0 if immediate. "
    << "Use longer intervals when waiting for external events.</interval>\n"
    << "  <global_context>OPTIONAL. Only include if the mission goal itself "
       "has changed. "
    << "Omit for routine progress.</global_context>\n"
    << "</last_cycle>\n\n"
    << "Often, it's ideal to split problems into multiple iterations rather "
       "than trying to handle most in 1 iteration";

  p << "3. Mission complete:\n"
    << "<final>\n"
    << "  <summary>Full breakdown of what was accomplished, issues "
       "encountered, "
    << "and final status. If failed, explain exactly why.</summary>\n"
    << "  <exit_code>success | failed | cancelled | "
       "pending_manual_intervention</exit_code>\n"
    << "</final>\n\n";

  // --- SCREENSHOTS ---
  p << "SCREENSHOTS\n"
    << "Save images to /Users/architect/Documents/Jarvis Screenshots/ to have "
       "them fed into the next iteration "
       "automatically. "
    << "Useful for verifying UI state or debugging layout. Images are removed "
       "after being fed.\n"
    << "Don't take more than 1 screen shot per iteration, and, whenever you "
       "capture a screenshot, make sure to end the iteration in the next "
       "cycle, as the screenshot needs to be fed.";

  // --- NOTIFICATIONS ---
  p << "NOTIFICATIONS\n"
    << "Notifications are external messages injected for this iteration only. "
    << "Bake them into global_context if they should affect future iterations. "
    << "Commands like EXIT NOW or ABORT MISSION must be acted on "
       "immediately.\n\n";

  // --- RULES ---
  p << "RULES\n"
    << "- Always output <thinking> first\n"
    << "- Max 10 tool calls per iteration\n"
    << "- If a tool fails, handle it in the same iteration if possible, "
       "otherwise exit with a clear error\n"
    << "- State must be detailed enough for the next iteration to resume "
       "without ambiguity\n"
    << "- Exit cleanly with a detailed error rather than looping on failure\n";

  return p.str();
}

inline std::string extract_tag_content(const std::string &input,
                                       const std::string &tag) {
  std::regex pattern("<" + tag + ">([\\s\\S]*?)</" + tag + ">");
  std::smatch match;
  if (std::regex_search(input, match, pattern))
    return match[1].str();
  return "";
}

inline std::string image_file_to_base64(const std::string &filepath) {
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return "";

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size))
    return "";

  return base64_encode(reinterpret_cast<unsigned char *>(buffer.data()),
                       buffer.size());
}

inline std::string get_and_clear_screenshot() {
  std::string screenshot_dir = "/Users/architect/Documents/Jarvis Screenshots";
  if (!std::filesystem::exists(screenshot_dir))
    return "";
  for (const auto &entry :
       std::filesystem::directory_iterator(screenshot_dir)) {
    auto ext = entry.path().extension();
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
      std::string b64 = image_file_to_base64(entry.path().string());
      std::filesystem::remove(entry.path());
      return b64;
    }
  }
  return "";
}

inline static const std::string tool_instructions =
    R"JARVIS(
    In the XML output, if you want to call a tool, output a <tool> tag with a <name> tag containing the tool name, and each argument in its own tag.
    Structure:
    <tool>
      <name>TOOL NAME</name>
      <arg1>value1</arg1>
      ...
    </tool>

    NEVER output more than 1 tool call per response, all subsequent tool calls will be ignored.
    Never output <last_cycle> or <exit_mission> alongside a tool call — you need the tool result before proceeding.

    After your tool call, stop generating immediately. Do not write another <tool> tag
    MULTIPLE TOOL CALLS IN THE SAME OUTPUT WILL RESULT IN MISSION FAILURE, TAKE THIS VERY SERIOUSLY.

    When you call a tool, the system runs it and in the next cycle it returns:
    <tool_result>
      <thinking>THINKING WHEN YOU MADE THAT CALL</thinking>
      <call>TOOL NAME</call>
      <result>TOOL OUTPUT</result>
    </tool_result>

    Your thinking output when calling a tool, should be a description about why you called that tool, so that you can keep track of it all.

    You are currently executing a live mission. This is not a simulation.
    Every tool call you make will be executed by the system in real time.
    Wait for real results before proceeding.
    Do not fabricate tool outputs or results.
    Do not write <tool_result> blocks — these are injected by the system, not you.

    ENVIRONMENT:
    - User folder: /Users/architect/
    - Projects folder: /Users/architect/Documents/Projects
    - To launch Chrome on port 9222: "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --remote-debugging-port=9222 --user-data-dir=/tmp/chrome-debug
    - Right now, this is a development environment, so, if you encounter anything strange, or stuck somewhere, please don't be hesistant to either query the user for information, or stop the mission giving a detailed breakdown of what happened.

    AVAILABLE TOOLS:
      query_user_data : Request information directly from the user that cannot be obtained any other way.
      Use for: credentials, passwords, API keys, personal preferences, ambiguous instructions, confirmation before irreversible actions. Do NOT use for: information you can find via other tools, decisions you can make yourself, routine progress updates. Args: request (string) — clearly describe exactly what you need and why, so the user can provide a precise answer.)JARVIS";
inline static const std::string anthropic_api_key =
    std::getenv("ANTHROPIC_API_KEY");

#define DEBUG TRUE

struct Mission {
  LLMConfig config;
  std::string global_context;
  std::string current_state;
  std::string ss_base64 = "";
  std::thread thread;

  void execute() {
// ANSI color codes
#define CLR_RESET "\033[0m"
#define CLR_GREEN "\033[32m"
#define CLR_ORANGE "\033[33m"
#define CLR_WHITE "\033[97m"
#define CLR_GRAY "\033[90m"

    auto divider = [](const std::string &label) {
      std::cout << CLR_GRAY
                << "\n══════════════════════════════════════════════════\n"
                << "  " << label << "\n"
                << "══════════════════════════════════════════════════\n"
                << CLR_RESET;
    };

    auto end_divider = [](const std::string &label) {
      std::cout << CLR_GRAY
                << "══════════════════════════════════════════════════\n"
                << "  END: " << label << "\n"
                << "══════════════════════════════════════════════════\n\n"
                << CLR_RESET;
    };

    for (uint32_t i = 0; i < 100; i++) {
      std::string prompt =
          construct_prompt(global_context, i, current_state, "NONE",
                           tool_instructions + "\n" + get_tool_instructions());

      divider("ITERATION #" + std::to_string(i) + " — PROMPT");
      std::cout << CLR_GREEN << prompt << CLR_RESET;
      end_divider("ITERATION #" + std::to_string(i) + " — PROMPT");

      std::string results{};
      std::string reply = query_anthropic(anthropic_api_key, prompt, ss_base64);

      if (reply.find("<tool>") != std::string::npos) {
        reply += "</tool>";
      }

      ss_base64 = "";

      divider("ITERATION #" + std::to_string(i) + " — INITIAL REPLY");
      std::cout << CLR_ORANGE << reply << CLR_RESET;
      end_divider("ITERATION #" + std::to_string(i) + " — INITIAL REPLY");

      pugi::xml_document doc;
      doc.load_string(("<root>" + reply + "</root>").c_str());

      auto root = doc.child("root");

      bool isFinal = false;
      uint32_t cycleID = 0;

      while (!isFinal && cycleID < 20) {
        pugi::xml_node thinking = root.child("thinking");
        auto tool_call = root.child("tool");
        auto is_last_cycle = root.child("last_cycle");
        auto final_node = root.child("final");

        if (tool_call) {
          std::string result = "";

          if (std::string(tool_call.child_value("name")) == "query_user_data") {
            std::string request = tool_call.child_value("request");
            std::cout << CLR_WHITE << "\n[AGENT REQUEST] " << request << "\n> "
                      << CLR_RESET;
            std::string userInput;
            std::getline(std::cin, userInput);
            result = userInput;
          }

          result = execute_tool(tool_call);

          results += "<tool_result>\n"
                     "  <thinking>" +
                     std::string(thinking.child_value()) +
                     "</thinking>\n"
                     "  <call>" +
                     tool_call.child_value("name") +
                     "</call>\n"
                     "  <result>" +
                     result +
                     "</result>\n"
                     "</tool_result>\n";

          std::cout << "\n================================ RESULTS "
                       "====================================\n"
                    << results
                    << "\n============================== END OF RESULTS "
                       "==============================\n"
                    << std::endl;

          reply = query_anthropic(anthropic_api_key,
                                  prompt + "\nCALLED TOOLS:\n" + results,
                                  ss_base64);

          doc.reset();
          doc.load_string(("<root>" + reply + "</root>").c_str());
          root = doc.child("root");

          divider("ITERATION #" + std::to_string(i) + " — CYCLE #" +
                  std::to_string(cycleID) + " REPLY");
          std::cout << CLR_WHITE << reply << CLR_RESET;
          end_divider("ITERATION #" + std::to_string(i) + " — CYCLE #" +
                      std::to_string(cycleID) + " REPLY");

        } else if (is_last_cycle) {
          std::string newCurrentState = is_last_cycle.child_value("state");
          std::string newGlobalContext =
              is_last_cycle.child_value("global_context");
          if (newGlobalContext != "")
            global_context = newGlobalContext;
          if (newCurrentState == "") {
            std::cerr << CLR_ORANGE << "FATAL: NO MISSION CURRENT STATE"
                      << CLR_RESET << "\n";
            return;
          }
          current_state = newCurrentState;
          isFinal = true;

          divider("ITERATION #" + std::to_string(i) + " — CYCLE END");
          std::cout << CLR_GRAY << "State: " << CLR_RESET << newCurrentState
                    << "\n";
          end_divider("ITERATION #" + std::to_string(i) + " — CYCLE END");

        } else if (final_node) {
          std::string mission_summary = final_node.child_value("summary");
          std::string exitStatus = final_node.child_value("exit_code");

          divider("MISSION COMPLETE");
          std::cout << CLR_GREEN << "Exit Code : " << exitStatus << "\n"
                    << "Summary   : " << mission_summary << CLR_RESET << "\n";
          end_divider("MISSION COMPLETE");
          return;
        }

        cycleID++;
      }

      ss_base64 = get_and_clear_screenshot();

      std::string interval_str = extract_tag_content(reply, "interval");
      long interval_ms = interval_str.empty() ? 5000 : std::stol(interval_str);

      std::cout << CLR_GRAY << "[ Iteration #" << i << " complete — sleeping "
                << interval_ms << "ms ]\n"
                << CLR_RESET;
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    std::cerr << CLR_ORANGE << "[Mission] hit 100 iteration limit\n"
              << CLR_RESET;
  }
};

inline std::vector<std::unique_ptr<Mission>> missions;
inline uint32_t currentMissionID{};

inline void dispatchMission(const LLMConfig &config, const std::string &context,
                            const std::string &goal) {
  std::cout << "\nMISSION LAUNCHED" << std::endl;
  auto mission = std::make_unique<Mission>();
  mission->config = config;
  mission->global_context = "Mission: " + context + "\nGoal: " + goal;
  mission->current_state = "";

  Mission *raw = mission.get();
  mission->thread = std::thread([raw]() { raw->execute(); });
  mission->thread.detach();

  missions.push_back(std::move(mission));
  currentMissionID++;
  std::cout << "\nMISSION SUCCESSFULLY CREATED\n" << std::endl;
}
