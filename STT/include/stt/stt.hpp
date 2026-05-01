#pragma once
#include "whisper.h"
#include <string>

struct STTConfig {
  const char *model_path =
      "third_party/whisper.cpp/models/ggml-large-v3-turbo.bin";
  int sample_rate = 16000;
  int n_threads = 4;

  // VAD
  float silence_threshold = 0.01f; // RMS energy below this = silence
  int silence_duration_ms = 1500;  // ms of silence before stopping
  int min_speech_ms = 300;         // min ms of speech before VAD kicks in
};

class STTClient {
public:
  explicit STTClient(const STTConfig &config);
  ~STTClient();

  STTClient(const STTClient &) = delete;
  STTClient &operator=(const STTClient &) = delete;

  std::string listen(int timeout_seconds = 10);

private:
  STTConfig m_config;
  whisper_context *m_ctx = nullptr;
};
