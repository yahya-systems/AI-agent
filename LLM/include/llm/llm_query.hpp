#pragma once
#include <string>

struct LLMConfig {
  std::string api_key;
  std::string url;
  std::string model;
  std::string system_prompt;
  int max_tokens = 200;
  bool debug = false;
};

namespace LLM {
LLMConfig get(const std::string &model_name, const std::string &api_key = "");
} // namespace LLM

std::string query_llm(const LLMConfig &config, const std::string &prompt);
