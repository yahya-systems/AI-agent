#include "pugixml.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>

static std::string result_buf;

static std::string run_applescript(const std::string &script) {
  if (script.empty())
    return "Error: missing script argument";

  std::ofstream f("/tmp/jarvis_script.applescript");
  if (!f.is_open())
    return "Error: could not open temp file for writing";
  f << script;
  f.close();

  FILE *pipe = popen("osascript /tmp/jarvis_script.applescript 2>&1", "r");
  if (!pipe)
    return "Failed to spawn process";

  std::string result;
  char buf[256];
  while (fgets(buf, sizeof(buf), pipe))
    result += buf;

  int pclose_status = pclose(pipe);
  int status = WEXITSTATUS(pclose_status);
  if (status != 0)
    return "Error (exit " + std::to_string(status) + "):\n" + result;

  return result.empty() ? "Done" : result;
}

extern "C" const char *execute(const char *xml_tool_call) {
  try {
    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml_tool_call);
    if (!parse_result) {
      result_buf = "Error: Failed to parse XML - " +
                   std::string(parse_result.description());
      return result_buf.c_str();
    }

    pugi::xml_node tool = doc.child("tool");
    if (!tool) {
      result_buf = "Error: Missing <tool> root element";
      return result_buf.c_str();
    }

    std::string script = tool.child_value("script");
    if (script.empty()) {
      result_buf = "Error: Missing or empty <script> argument";
      return result_buf.c_str();
    }

    result_buf = run_applescript(script);
  } catch (const std::exception &e) {
    result_buf = std::string("Error: ") + e.what();
  }

  return result_buf.c_str();
}
