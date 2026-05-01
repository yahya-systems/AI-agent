#include "tools/tools.hpp"
#include "pugixml/pugixml.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
namespace fs = std::filesystem;

class Tool {
public:
  std::string short_desc;
  std::string full_desc;

  Tool(const std::string &tool_dir) {
    std::string name = fs::path(tool_dir).filename().string();

    // load dylib
    m_handle = dlopen((tool_dir + "/" + name + ".dylib").c_str(), RTLD_NOW);
    if (!m_handle)
      std::cerr << "[Tool] dlopen failed for " << name << ": " << dlerror()
                << "\n";
    else
      m_fn = (const char *(*)(const char *))dlsym(m_handle, "execute");

    // parse instructions.txt
    std::ifstream f(tool_dir + "/instructions.txt");
    if (!f) {
      std::cerr << "[Tool] no instructions.txt for " << name << "\n";
      return;
    }

    std::getline(f, short_desc);

    std::string blank;
    std::getline(f, blank); // skip empty line

    std::string line;
    while (std::getline(f, line))
      full_desc += line + "\n";
  }

  std::string execute(pugi::xml_node node) const {
    if (!m_fn)
      return "Error: tool not loaded";
    std::ostringstream ss;
    node.print(ss);
    std::string xml = ss.str();
    const char *result = m_fn(xml.c_str());
    return result ? result : "";
  }

  ~Tool() {
    if (m_handle)
      dlclose(m_handle);
  }

  // no copy — dylib handle isn't copyable
  Tool(const Tool &) = delete;
  Tool &operator=(const Tool &) = delete;

  // move is fine
  Tool(Tool &&other) noexcept
      : short_desc(std::move(other.short_desc)),
        full_desc(std::move(other.full_desc)), m_handle(other.m_handle),
        m_fn(other.m_fn) {
    other.m_handle = nullptr;
    other.m_fn = nullptr;
  }

private:
  void *m_handle = nullptr;
  const char *(*m_fn)(const char *) = nullptr;
};

std::unordered_map<std::string, Tool> tools;

void load_tools() {
  std::string tools_dir = "Tools/tool_library";

  if (!fs::exists(tools_dir) || !fs::is_directory(tools_dir)) {
    std::cerr << "[Tools] directory not found: " << tools_dir << "\n";
    return;
  }

  for (const auto &entry : fs::directory_iterator(tools_dir)) {
    if (!entry.is_directory())
      continue;
    std::string name = entry.path().filename().string();
    tools.emplace(name, entry.path().string());
    std::cout << "[Tools] loaded: " << name << "\n";
  }
}

std::string load_tool(const std::string &name) {
  std::string path = "Tools/tool_library/" + name;
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return "[Tools] tool not found: " + path + "\n";
  }
  tools.emplace(name, path);
  return "[Tools] loaded: " + name;
}

std::string get_tool_instructions() {
  std::string result =
      "get_tool_info : Returns the full description of a tool "
      "by name. Args: name (string).\n"
      "load_tool : Loads a tool into runtime by name. Args: toolName (string)";
  for (const auto &[name, tool] : tools)
    result += name + ": " + tool.short_desc + "\n";
  return result;
}

std::string execute_tool(pugi::xml_node call) {
  std::string name = call.child("name").text().get();

  if (name == "get_tool_info") {
    std::string target = call.child("tool_name").text().get();
    auto target_it = tools.find(target);
    if (target_it == tools.end())
      return "Unknown tool: " + target;
    return target_it->second.full_desc;
  }

  if (name == "load_tool") {
    std::string target = call.child("toolName").text().get();
    return load_tool(target);
  }

  auto it = tools.find(name);
  if (it == tools.end())
    return "Unknown tool: " + name;

  return it->second.execute(call);
}
