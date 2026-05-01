#define MINIAUDIO_IMPLEMENTATION
#include "whisper.cpp/examples/miniaudio.h"

#include "stt/stt.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

STTClient::STTClient(const STTConfig &config) : m_config(config) {
  whisper_context_params cparams = whisper_context_default_params();
  m_ctx = whisper_init_from_file_with_params(config.model_path, cparams);
  if (!m_ctx)
    std::cerr << "Failed to load whisper model from: " << config.model_path
              << "\n";
}

STTClient::~STTClient() {
  if (m_ctx)
    whisper_free(m_ctx);
}

struct RecordContext {
  std::vector<float> *pcm;
  std::atomic<bool> *done;
  float silence_threshold;
  int silence_duration_ms;
  int min_speech_ms;
  int sample_rate;

  // internal state
  int silent_samples = 0;
  int speech_samples = 0;
  bool speech_started = false;
};

static void record_callback(ma_device *dev, void *, const void *input,
                            ma_uint32 frames) {
  auto *ctx = (RecordContext *)dev->pUserData;
  const float *in = (const float *)input;

  // compute RMS of this chunk
  float sum = 0.0f;
  for (ma_uint32 i = 0; i < frames; i++)
    sum += in[i] * in[i];
  float rms = std::sqrt(sum / frames);

  // append to buffer
  size_t old_size = ctx->pcm->size();
  ctx->pcm->resize(old_size + frames);
  memcpy(ctx->pcm->data() + old_size, in, frames * sizeof(float));

  ctx->speech_samples += frames;

  int silence_samples_threshold =
      (ctx->silence_duration_ms * ctx->sample_rate) / 1000;
  int min_speech_samples = (ctx->min_speech_ms * ctx->sample_rate) / 1000;

  if (rms < ctx->silence_threshold) {
    ctx->silent_samples += frames;
    if (ctx->speech_started &&
        ctx->silent_samples >= silence_samples_threshold &&
        ctx->speech_samples >= min_speech_samples) {
      ctx->done->store(true);
    }
  } else {
    ctx->silent_samples = 0;
    ctx->speech_started = true;
  }
}

std::string STTClient::listen(int timeout_seconds) {
  if (!m_ctx)
    return "";

  std::vector<float> pcm;
  std::atomic<bool> done{false};

  pcm.reserve(m_config.sample_rate * timeout_seconds);

  RecordContext rec_ctx = {
      .pcm = &pcm,
      .done = &done,
      .silence_threshold = m_config.silence_threshold,
      .silence_duration_ms = m_config.silence_duration_ms,
      .min_speech_ms = m_config.min_speech_ms,
      .sample_rate = m_config.sample_rate,
  };

  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 1;
  cfg.sampleRate = m_config.sample_rate;
  cfg.dataCallback = record_callback;
  cfg.pUserData = &rec_ctx;

  ma_device device;
  if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
    std::cerr << "Failed to init audio device\n";
    return "";
  }

  ma_device_start(&device);

  // wait for VAD to signal done or timeout
  auto start = std::chrono::steady_clock::now();
  while (!done.load()) {
    struct timespec ts = {0, 10000000}; // 10ms
    nanosleep(&ts, nullptr);
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >=
        timeout_seconds)
      break;
  }

  ma_device_stop(&device);
  ma_device_uninit(&device);

  if (pcm.empty())
    return "";

  whisper_full_params wparams =
      whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  wparams.language = "en";
  wparams.n_threads = m_config.n_threads;
  wparams.print_progress = false;
  wparams.print_realtime = false;
  wparams.print_timestamps = false;
  wparams.no_timestamps = true;
  wparams.single_segment = true;

  if (whisper_full(m_ctx, wparams, pcm.data(), (int)pcm.size()) != 0) {
    std::cerr << "Whisper inference failed\n";
    return "";
  }

  std::string result;
  int n = whisper_full_n_segments(m_ctx);
  for (int i = 0; i < n; i++)
    result += whisper_full_get_segment_text(m_ctx, i);

  return result;
}
