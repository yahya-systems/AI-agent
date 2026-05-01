#include "LLM/include/llm/llm_query.hpp"
#include "Missions/Mission.hpp"
#include "TTS/tts.hpp"
#include "llm/llm_query.hpp"
#include "pugixml/pugixml.hpp"
#include "stt/stt.hpp"
#include "tools/tools.hpp"
#include <iostream>

// bool is_tool_call(const std::string &reply, nlohmann::json &out) {
//   auto start = reply.find('{');
//   auto end = reply.rfind('}');
//   if (start == std::string::npos || end == std::string::npos)
//     return false;
//   try {
//     out = nlohmann::json::parse(reply.substr(start, end - start + 1));
//     return out.contains("tool");
//   } catch (...) {
//     return false;
//   }
// }

int main() {
  // LLMConfig llm = LLM::gemma4_local();
  LLMConfig llm =
      LLM::get("claude-sonnet-4-20250514", std::getenv("ANTHROPIC_API_KEY"));

  constexpr bool debug = false;

  llm.debug = debug;

  llm.system_prompt = R"JARVIS(
You are Jarvis, a concise AI assistant. Keep your responses short.

Your output should be an XML, always begin by speech, wrapped in <speech></speech>, which should contain your talking.

Tool calling:
If you want to call a tool, instead of <speech>, output a <think> block followed by a <tool> block. Do NOT output <speech> when calling a tool — the user does not hear your thinking.
<think>what I have done and what I need to do next</think>
<tool>
  <name>tool_name</name>
  <arg1>value</arg1>
</tool>

ALWAYS output <think> before every tool call, no exceptions. Even for simple calls.

Don't make the think block too long.

If you are not calling a tool, output only <speech> with your response.
NEVER output more than 1 tool call in a single message.

WRONG — speech with tool:
<speech>Let me search that.</speech>
<tool><name>web_search</name><query>bitcoin price</query></tool>

WRONG — multiple tools:
<think>need both prices</think>
<tool><name>web_search</name><query>bitcoin price</query></tool>
<tool><name>web_search</name><query>ethereum price</query></tool>

RIGHT — tool call:
<think>user wants bitcoin price, I'll search for it</think>
<tool><name>web_search</name><query>bitcoin price</query></tool>

RIGHT — no tool:
<speech>It is currently ten seventeen PM.</speech>

when calling a tool, the system picks it up, and executes it, then returns it's result under this format :
Tool called (thinking : YOUR THINKING WHEN YOU CALLED THAT TOOL) : TOOL CALL XML, Result : TOOL CALL RESULT

ENVIRONMENT:
- As This is currently a development environment, if you encounter any issues with the tools, make sure to not try to fight it or work around, tell immediately, just output a detailed explanation of what exactly happened.
- User folder: /Users/architect/
- Projects folder: /Users/architect/Documents/Projects
- <speech> contains exactly what will be read aloud to the user. Keep it natural, no URLs, no file paths, no code.

For anything requiring the web, use the run_python tool with playwright, in order to launch chrome on port 9222, run this command
```"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --remote-debugging-port=9222 --user-data-dir=/tmp/chrome-debug```

AVAILABLE TOOLS:
)JARVIS";

  llm.system_prompt +=
      R"(
      dispatch_mission: Launch a background agent that runs a mission autonomously. The agent runs in a loop, executes tools, updates its state, and sleeps between iterations. Does not block the main conversation.
      Args: goal (string) The mission objective. This is the permanent goal that guides all iterations. Example: "Monitor WhatsApp Web for messages from a friend and notify me when one arrives."

      mute: a tool call that's going to mute Jarvis until the user brings it back, use it anytime the user says something like "mute", any type where you think you need to be going quiet. Args : none

      query_prompt_txt : a tool call, that's going to evade speed to text, if the user wants to provide a prompt in text, or said he wants to switch to text mode, or maybe you need some extra information during a cycle, then call this tool, it's output will be exclusively the prompt provided. 
      Args : request (string) a string that will be shown as Jarvis requested a text prompt saying [request]
  )";

  load_tools();

  llm.max_tokens = 10000;

  STTConfig stt_config;
  STTClient stt(stt_config); // loads model once here

  TTSClient jarvis{};
  std::string memory;
  memory.reserve(4096);

  while (true) {
    std::cout << "Listening...\n";
    std::string input = stt.listen(180);

    if (input.empty())
      continue;
    if (input.find("Exit") != std::string::npos)
      break;

    std::cout << "You: " << input << "\n";
    memory += "You : " + input + "\n";

    if (debug)
      std::cout << "--------- PROMPT -----------\n" << memory << "";

    std::string reply = query_llm(llm, get_tool_instructions() + memory);

    if (debug)
      std::cout << "--------- REPLY ----------\n"
                << reply << "\n------------ END OF REPLY -------------"
                << std::endl;

    std::string results;
    results.reserve(1024);

    pugi::xml_document doc;
    doc.load_string(("<root>" + reply + "</root>").c_str());
    auto root = doc.child("root");
    pugi::xml_node toolNode = root.child("tool");

    std::string speech = root.child_value("speech");

    while (toolNode) {
      std::string toolResult;
      std::string toolName = toolNode.child_value("name");

      if (toolName == "dispatch_mission") {
        std::string missionGoal = toolNode.child_value("goal");
        dispatchMission(llm, "", missionGoal);
        toolResult = "SUCCESS, MISSION EXECUTING IN THE BACKGROUND";
      } else if (toolName == "mute") {
        auto mute_start = std::chrono::steady_clock::now();
        std::cout << "Jarvis has been muted, enter any input to unmute\n";
        char x;
        std::cin >> x;
        auto mute_end = std::chrono::steady_clock::now();
        auto mute_duration = std::chrono::duration_cast<std::chrono::seconds>(
            mute_end - mute_start);
        std::cout << "Jarvis back online\n";
        toolResult = "back online, muted for " +
                     std::to_string(mute_duration.count()) + " seconds";
      } else if (toolName == "query_prompt_txt") {
        std::string request = toolNode.child_value("request");
        std::cout << "Jarvis requested a text prompt: " << request << "\n>> ";
        std::getline(std::cin, toolResult);
      } else {
        std::ostringstream ss;
        toolNode.print(ss);
        std::cout << "TOOL NODE: " << ss.str() << "END" << "\n";
        toolResult = execute_tool(toolNode);
      }

      std::cerr << "tool called\n";

      results +=
          "Tool called (thinking : " + std::string(root.child_value("think")) +
          ") :" + serialize_node(toolNode) + "\nResult: " + toolResult + "\n";

      std::cout << "------- TOOL RESULT -------\n\n"
                << results << "---------------------" << std::endl;

      reply = query_llm(llm, get_tool_instructions() + memory + results + "\n");

      std::cout << "--------- LLM REPLY -----------\n"
                << reply << "-------------------" << std::endl;

      doc.reset();
      doc.load_string(("<root>" + reply + "</root>").c_str());
      root = doc.child("root");
      toolNode = root.child("tool");
    }

    speech = root.child_value("speech");

    memory += "Jarvis : " + speech + "\n";
    std::cout << "Jarvis: " << speech << "\n";

    jarvis.speak(speech);
  }

  return 0;
}
