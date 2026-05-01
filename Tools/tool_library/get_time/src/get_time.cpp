#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

static std::string result_buf;

extern "C" const char *execute(const char *args) {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
  result_buf = ss.str();
  return result_buf.c_str();
}
