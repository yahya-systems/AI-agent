#include "json.hpp"
#include "pugixml.hpp"
#include <curl/curl.h>
#include <string>

static std::string result_buf;
static const std::string API_KEY =
    "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";

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             std::string *out) {
  out->append((char *)contents, size * nmemb);
  return size * nmemb;
}

static std::string search(pugi::xml_node &root) {
  pugi::xml_node query_node = root.child("query");
  if (!query_node)
    return "Error: missing query argument";

  int max_results = root.child("max_results")
                        ? std::stoi(root.child("max_results").text().get())
                        : 5;

  nlohmann::json body = {{"api_key", API_KEY},
                         {"query", query_node.text().get()},
                         {"max_results", max_results},
                         {"include_answer", true},
                         {"search_depth", "basic"}};

  std::string body_str = body.dump();
  std::string response;

  CURL *curl = curl_easy_init();
  if (!curl)
    return "Failed to init curl";

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/search");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

  curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  try {
    auto j = nlohmann::json::parse(response);
    std::string result;
    if (j.contains("answer") && !j["answer"].is_null())
      result += "Answer: " + j["answer"].get<std::string>() + "\n\n";
    if (j.contains("results"))
      for (const auto &r : j["results"]) {
        result += "- " + r.value("title", "") + "\n";
        result += "  " + r.value("url", "") + "\n";
        result += "  " + r.value("content", "") + "\n\n";
      }
    return result.empty() ? "No results found" : result;
  } catch (const std::exception &e) {
    return std::string("Error parsing response: ") + e.what();
  }
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

    result_buf = search(root);
  } catch (const std::exception &e) {
    result_buf = std::string("Error: ") + e.what();
  }
  return result_buf.c_str();
}
