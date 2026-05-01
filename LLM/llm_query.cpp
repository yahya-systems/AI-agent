#include "llm/llm_query.hpp"
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace LLM {
inline std::unordered_map<std::string, LLMConfig> models = {
    {"deepseek-chat",
     {
         .api_key = "",
         .url = "https://api.deepseek.com/v1/chat/completions",
         .model = "deepseek-chat",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"deepseek-v4-flash",
     {
         .api_key = "",
         .url = "https://api.deepseek.com/v1/chat/completions",
         .model = "deepseek-v4-flash",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"deepseek-v4-pro",
     {
         .api_key = "",
         .url = "https://api.deepseek.com/v1/chat/completions",
         .model = "deepseek-v4-pro",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"claude-sonnet-4-20250514",
     {
         .api_key = "",
         .url = "https://api.anthropic.com/v1/messages",
         .model = "claude-sonnet-4-6",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"gemma-4-31b-it",
     {
         .api_key = "",
         .url = "https://generativelanguage.googleapis.com/v1beta/openai/chat/"
                "completions",
         .model = "gemma-4-31b-it",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"llama-3.3-70b-versatile",
     {
         .api_key = "",
         .url = "https://api.groq.com/openai/v1/chat/completions",
         .model = "llama-3.3-70b-versatile",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"openai/gpt-oss-120b",
     {
         .api_key = "",
         .url = "https://api.groq.com/openai/v1/chat/completions",
         .model = "openai/gpt-oss-120b",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"glm-4.5-flash",
     {
         .api_key = "",
         .url = "https://open.bigmodel.cn/api/paas/v4/chat/completions",
         .model = "glm-4.5-flash",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"gemma4",
     {
         .api_key = "ollama",
         .url = "http://localhost:11434/v1/chat/completions",
         .model = "gemma4",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
    {"qwen3:8b",
     {
         .api_key = "ollama",
         .url = "http://localhost:11434/v1/chat/completions",
         .model = "qwen3:8b",
         .system_prompt = "You are Jarvis, an AI assistant.",
     }},
};

LLMConfig get(const std::string &model_name, const std::string &api_key) {
  auto it = models.find(model_name);
  if (it == models.end()) {
    std::cerr << "[LLM] unknown model: " << model_name << "\n";
    return {};
  }
  LLMConfig config = it->second;
  if (!api_key.empty())
    config.api_key = api_key;
  return config;
}

} // namespace LLM

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             std::string *out) {
  out->append((char *)contents, size * nmemb);
  return size * nmemb;
}

auto strip_thinking = [](const std::string &s) -> std::string {
  std::string result = s;
  // all known thinking tag pairs
  for (auto &[open, close] : std::vector<std::pair<std::string, std::string>>{
           {"<thought>", "</thought>"},
           {"<think>", "</think>"},
           {"<thinking>", "</thinking>"},
       }) {
    size_t start = result.find(open);
    while (start != std::string::npos) {
      size_t end = result.find(close, start);
      if (end == std::string::npos)
        break;
      result.erase(start, end - start + close.size());
      start = result.find(open);
    }
  }
  size_t first = result.find_first_not_of(" \n\r\t");
  return first == std::string::npos ? "" : result.substr(first);
};

std::string query_llm(const LLMConfig &config,
                      const std::string &user_message) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;

  // 1. Detect if it's an Anthropic API call
  bool is_anthropic = config.url.find("anthropic.com") != std::string::npos;

  nlohmann::json messages = nlohmann::json::array();
  nlohmann::json body;

  // 2. Construct Payload based on Provider
  if (is_anthropic) {
    body["system"] = config.system_prompt;
    messages.push_back({{"role", "user"}, {"content", user_message}});
  } else {
    messages.push_back({{"role", "system"}, {"content", config.system_prompt}});
    messages.push_back({{"role", "user"}, {"content", user_message}});
  }

  body["model"] = config.model;
  body["max_tokens"] = config.max_tokens;
  body["messages"] = messages;
  std::string body_str = body.dump();

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  // 3. Construct Headers based on Provider
  if (is_anthropic) {
    headers =
        curl_slist_append(headers, ("x-api-key: " + config.api_key).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
  } else {
    headers = curl_slist_append(
        headers, ("Authorization: Bearer " + config.api_key).c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, config.url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

  if (res != CURLE_OK) {
    std::cerr << "\n" RED "[LLM] curl error: " << curl_easy_strerror(res)
              << RESET "\n\n";
    return "";
  }

  if (response.empty()) {
    std::cerr << "\n" RED "[LLM] empty response from server" RESET "\n\n";
    return "";
  }

  if (config.debug)
    std::cerr << "\n" YELLOW "[LLM] raw: " << response << RESET "\n\n";

  try {
    auto j = nlohmann::json::parse(response);

    if (j.contains("error")) {
      std::string msg = j["error"].contains("message")
                            ? j["error"]["message"].get<std::string>()
                            : j["error"].dump();
      std::cerr << "\n" RED "[LLM] API error: " << msg << RESET "\n\n";
      return "";
    }

    // 4. Parse Response based on Provider
    if (is_anthropic) {
      // Anthropic format: { "content": [ { "type": "text", "text": "..." } ] }
      if (j.contains("content") && j["content"].is_array() &&
          !j["content"].empty()) {
        auto &content_block = j["content"][0];
        if (content_block.contains("text") &&
            !content_block["text"].is_null()) {
          return strip_thinking(content_block["text"].get<std::string>());
        }
      }
      std::cerr << "\n" RED
                   "[LLM] no usable content in Anthropic response" RESET "\n\n";
      return "";
    } else {
      // OpenAI format: { "choices": [ { "message": { "content": "..." } } ] }
      if (!j.contains("choices") || j["choices"].empty()) {
        std::cerr << "\n" RED "[LLM] no choices in response" RESET "\n\n";
        return "";
      }

      auto &msg = j["choices"][0]["message"];

      if (msg.contains("content") && !msg["content"].is_null()) {
        std::string content = msg["content"].get<std::string>();
        if (!content.empty())
          return strip_thinking(content);
      }

      if (msg.contains("reasoning") && !msg["reasoning"].is_null()) {
        std::string reasoning = msg["reasoning"].get<std::string>();
        auto start = reasoning.rfind('{');
        auto end = reasoning.rfind('}');
        if (start != std::string::npos && end != std::string::npos &&
            end > start)
          return reasoning.substr(start, end - start + 1);
      }

      std::cerr << "\n" RED "[LLM] no usable content in response" RESET "\n\n";
      return "";
    }

  } catch (const std::exception &e) {
    std::cerr << "\n" RED "[LLM] parse error: " << e.what() << RESET "\n\n";
    return "";
  }

#undef RED
#undef YELLOW
#undef RESET
}
