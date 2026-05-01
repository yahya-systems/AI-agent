#include "pugixml.hpp"
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>

static std::string result_buf;
static std::string base =
    "/Users/architect/Documents/Projects/Prometheus/jarvis_py";
static std::string venv = base + "/venv";
static std::string pip = venv + "/bin/pip3";
static std::string python = venv + "/bin/python3";
static std::string temp = base + "/temp/python_exec.py";

static void install_packages(pugi::xml_node &root) {
  pugi::xml_node packages = root.child("packages");
  if (!packages)
    return;
  for (pugi::xml_node pkg : packages.children("package"))
    system(
        (pip + " install " + pkg.text().get() + " > /dev/null 2>&1").c_str());
}

static std::string run_pipe(const std::string &cmd) {
  FILE *pipe = popen(cmd.c_str(), "r");
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

static std::string variant_code(pugi::xml_node &root) {
  pugi::xml_node code_node = root.child("code");
  if (!code_node)
    return "Error: missing code argument";
  install_packages(root);
  std::ofstream f(temp);
  f << code_node.text().get();
  f.close();
  return run_pipe(python + " " + temp + " 2>&1");
}

static std::string variant_file(pugi::xml_node &root) {
  pugi::xml_node path_node = root.child("path");
  if (!path_node)
    return "Error: missing path argument";
  install_packages(root);
  return run_pipe(python + " " + path_node.text().get() + " 2>&1");
}

static std::string variant_repl(pugi::xml_node &root) {
  pugi::xml_node expr_node = root.child("expression");
  if (!expr_node)
    return "Error: missing expression argument";
  std::string code = "print(" + std::string(expr_node.text().get()) + ")";
  std::ofstream f(temp);
  f << code;
  f.close();
  return run_pipe(python + " " + temp + " 2>&1");
}

extern "C" const char *execute(const char *args_xml) {
  try {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(args_xml);
    if (!result) {
      result_buf =
          std::string("Error: XML parse failed: ") + result.description();
      return result_buf.c_str();
    }

    pugi::xml_node root = doc.child("tool");
    if (!root) {
      result_buf = "Error: missing <tool> root element";
      return result_buf.c_str();
    }

    std::string variant = root.child("variant")
                              ? std::string(root.child("variant").text().get())
                              : "code";

    if (variant == "code")
      result_buf = variant_code(root);
    else if (variant == "file")
      result_buf = variant_file(root);
    else if (variant == "repl")
      result_buf = variant_repl(root);
    else
      result_buf = "Error: unknown variant: " + variant;

  } catch (const std::exception &e) {
    result_buf = std::string("Error: ") + e.what();
  }

  return result_buf.c_str();
}
