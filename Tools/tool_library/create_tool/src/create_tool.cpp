#include <array>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;

static char result_buffer[4096];

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             std::string *out) {
  out->append((char *)contents, size * nmemb);
  return size * nmemb;
}

static std::string extract_tag(const std::string &text,
                               const std::string &tag) {
  std::string open = "<" + tag + ">";
  std::string close = "</" + tag + ">";
  size_t start = text.find(open);
  size_t end = text.find(close);
  if (start == std::string::npos || end == std::string::npos)
    return "";
  start += open.size();
  return text.substr(start, end - start);
}

static std::string parse_opus_text(const std::string &raw) {
  auto j = nlohmann::json::parse(raw, nullptr, false);
  if (j.is_discarded())
    return "";
  std::string text;
  for (auto &block : j["content"])
    if (block["type"] == "text")
      text += block["text"].get<std::string>();
  return text;
}

static bool write_file(const fs::path &path, const std::string &content) {
  fs::create_directories(path.parent_path());
  std::ofstream f(path);
  if (!f.is_open())
    return false;
  f << content;
  return true;
}

static std::string shell(const std::string &cmd) {
  std::array<char, 512> buf;
  std::string output;
  FILE *pipe = popen((cmd + " 2>&1").c_str(), "r");
  if (!pipe)
    return "error: popen failed";
  while (fgets(buf.data(), buf.size(), pipe))
    output += buf.data();
  pclose(pipe);
  return output;
}

static const std::string system_prompt = R"OPUS(
You are a C++ systems programmer writing dylib tools for a plugin system.

Rules:
- Output the complete C++ source wrapped in <source></source> tags.
- Output the instructions.txt contents inside <instructions></instructions> tags, instructions.txt, is simply a file that contains information the agent might need about the tool
the first line should be a brief description, still a bit detailed, and the argument names for the tool call json, then a blank line, then a detailed description which contains a long detailed description about the tool.
- Output required linker flags in <libs></libs> tags. Example: <libs>-lcurl -lsqlite3</libs>
- The entry point must be: extern "C" const char* execute(const char* args_json)
- Use a static char result_buffer[4096] for the return value.
- Parse args using nlohmann/json: auto args = nlohmann::json::parse(args_json);
- Return results as plain string via snprintf into result_buffer.
- Use libcurl for any HTTP requests.
- Wrap everything in try/catch, return "error: ..." on failure.
- No main() function.
- Include only what you need: nlohmann/json.hpp, curl/curl.h, string, cstring, cstdio, filesystem, etc.
- Compiled with: clang++ -shared -fPIC -std=c++17 -o <name>.dylib <name>.cpp <libs>
)OPUS";

static std::string call_opus(const std::vector<nlohmann::json> &messages) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "error: curl init failed";

  std::string response;
  const char *api_key_env =
      "sk-ant-api03-3povbq1EKuDMjQyXwS6-Z8fw7xxarK9dPPx-"
      "73hF21vxOjDuYeZwq0NSGFPjrGqp4MMuSJZeq2T8-i6G8m9SwA-JFUxYwAA";
  if (!api_key_env)
    return "error: ANTHROPIC_API_KEY not set";
  std::string api_key = api_key_env;

  nlohmann::json body = {{"model", "claude-opus-4-6"},
                         {"max_tokens", 4096},
                         {"system", system_prompt},
                         {"messages", messages}};

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

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
    return std::string("error: ") + curl_easy_strerror(res);

  return response;
}

extern "C" const char *execute(const char *args_json) {
  try {
    auto args = nlohmann::json::parse(args_json);
    std::string tool_name = args.at("tool_name");
    std::string tool_desc = args.at("tool_description");

    fs::path tool_dir = fs::path("Tools/tool_library") / tool_name;
    fs::path src_path = tool_dir / "src" / (tool_name + ".cpp");
    fs::path dylib_path = tool_dir / (tool_name + ".dylib");
    fs::path instr_path = tool_dir / "instructions.txt";

    std::vector<nlohmann::json> messages = {
        {{"role", "user"},
         {"content",
          "Tool name: " + tool_name +
              "\n"
              "Description: " +
              tool_desc +
              "\n\n"
              "Write the complete C++ dylib source for this tool."}}};

    std::string source, libs, instructions, compiler_error;
    int attempts = 0;
    const int max_attempts = 3;

    while (attempts < max_attempts) {
      attempts++;

      std::string raw = call_opus(messages);
      std::string text = parse_opus_text(raw);
      if (text.empty()) {
        snprintf(result_buffer, sizeof(result_buffer),
                 "error: empty response on attempt %d", attempts);
        return result_buffer;
      }

      messages.push_back({{"role", "assistant"}, {"content", text}});

      source = extract_tag(text, "source");
      libs = extract_tag(text, "libs");
      instructions = extract_tag(text, "instructions");

      if (source.empty()) {
        snprintf(result_buffer, sizeof(result_buffer),
                 "error: no <source> tag in response on attempt %d", attempts);
        return result_buffer;
      }

      if (!write_file(src_path, source)) {
        snprintf(result_buffer, sizeof(result_buffer),
                 "error: failed to write source file");
        return result_buffer;
      }

      std::string compile_cmd = "clang++ -shared -fPIC -std=c++17 -o " +
                                dylib_path.string() + " " + src_path.string() +
                                (libs.empty() ? "" : " " + libs);

      compiler_error = shell(compile_cmd);

      if (compiler_error.empty()) {
        write_file(instr_path, instructions);
        snprintf(result_buffer, sizeof(result_buffer),
                 "ok: %s compiled successfully after %d attempt(s)",
                 tool_name.c_str(), attempts);
        return result_buffer;
      }

      messages.push_back(
          {{"role", "user"},
           {"content", "Compilation failed:\n" + compiler_error +
                           "\n\nFailed source:\n<source>" + source +
                           "</source>" +
                           "\n\nFix it and output the corrected version."}});
    }

    snprintf(result_buffer, sizeof(result_buffer),
             "error: failed to compile after %d attempts. Last error: %s",
             max_attempts, compiler_error.substr(0, 300).c_str());
    return result_buffer;

  } catch (std::exception &e) {
    snprintf(result_buffer, sizeof(result_buffer), "error: %s", e.what());
    return result_buffer;
  }
}
