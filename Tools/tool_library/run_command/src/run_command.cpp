#include "pugixml.hpp"
#include <cstdlib>
#include <string>
#include <sys/wait.h>

static std::string result_buf;

static std::string run_command(const std::string &command,
                               const std::string &work_dir) {
  std::string full_command = "cd \"" + work_dir + "\" && " + command;

  FILE *pipe = popen((full_command + " 2>&1").c_str(), "r");
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

  return result.empty() ? "No output" : result;
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

    std::string command = tool.child_value("command");
    if (command.empty()) {
      result_buf = "Error: Missing or empty <command> argument";
      return result_buf.c_str();
    }

    const char *home_env = std::getenv("HOME");
    std::string default_dir = home_env ? home_env : "/";
    std::string work_dir = tool.child("working_directory")
                               ? tool.child_value("working_directory")
                               : default_dir;

    result_buf = run_command(command, work_dir);
  } catch (const std::exception &e) {
    result_buf = std::string("Error: ") + e.what();
  }

  return result_buf.c_str();
}
