#pragma once

#include "prompter_engine.h"
#include <QString>

namespace pcm::prompter {

// An OpenAI-compatible chat/completions endpoint chosen by the user.
struct CloudConfig {
  QString baseUrl;  // e.g. https://api.example.com/v1; "/chat/completions" is appended
  QString model;
  QString apiKey;
  int timeoutMs = 120000;  // per attempt
  int retryDelayMs = 700;  // before the single retry
  int maxOutputTokens = 2048;
};

// generate() blocks the calling thread (it runs a private event loop, so any
// thread works). A nonempty schema is sent as response_format json_schema with
// strict:true and there is no other fallback. One retry on a network error,
// 429 and 5xx. Failures throw PipelineError with a fixed code:
//   "cloud no key", "cloud auth" (401/403), "cloud model" (404, unknown
//   model), "cloud unsupported" (provider rejects json_schema), "cloud rate
//   limit", "cloud server", "cloud network", "cloud timeout", "cloud request",
//   "output truncated" (empty or cut-off reply), "cancelled".
// Prompts, payloads, replies and the key are never logged.
std::unique_ptr<PrompterEngine> createCloudEngine(const CloudConfig &config);

struct CloudProbe {
  bool ok = false;
  // "" when ok; else "no key", "auth", "model", "unsupported", "rate limit",
  // "server", "network", "timeout", "request".
  QString reason;
};
// "Test connection": one request with a test schema. A provider that rejects
// json_schema or answers off-schema is not accepted. Blocking.
CloudProbe probeCloud(const CloudConfig &config);

} // namespace pcm::prompter
