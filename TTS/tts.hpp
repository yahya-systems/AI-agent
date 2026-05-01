#pragma once
#include <chrono>
#include <cstddef>
#include <curl/curl.h>
#pragma once
#include <chrono>
#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

inline size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *userp) {
  size_t totalSize = size * nmemb;
  userp->append((char *)contents, totalSize);
  return totalSize;
}

inline bool is_playing() {
  CURL *curl = curl_easy_init();
  if (!curl)
    return false;
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/status");
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK)
    return false;
  try {
    return nlohmann::json::parse(response)["playing"].get<bool>();
  } catch (...) {
    return false;
  }
}

class TTSClient {
  CURL *curl;
  struct curl_slist *headers;

public:
  TTSClient() {
    curl = curl_easy_init();
    if (!curl) {
      std::cerr << "Failed To Create Curl object\n";
      return;
    }
    headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/play");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     [](void *, size_t size, size_t nmemb, void *) -> size_t {
                       return size * nmemb;
                     });
  }

  void speak(const std::string &text) {
    if (text.empty() || !curl)
      return;
    std::string payload = nlohmann::json{{"text", text}}.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_perform(curl);
    waitUntilCompleted();
  }

  void speak_async(const std::string &text) {
    if (text.empty() || !curl)
      return;
    std::string payload = nlohmann::json{{"text", text}}.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_perform(curl);
  }

  void waitUntilCompleted() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    while (is_playing())
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  ~TTSClient() {
    if (curl)
      curl_easy_cleanup(curl);
    if (headers)
      curl_slist_free_all(headers);
  }
};
