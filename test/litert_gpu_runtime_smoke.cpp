// Standalone functional smoke; no Qt, app settings or external inference process.
#include "sessio_litert.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace sessio::litert;
namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
}

int main(int argc, char **argv) try {
  require(argc >= 2, "usage: smoke model [cpu|fallback]");
  const std::string mode = argc > 2 ? argv[2] : "gpu";
  ModelOptions options;
  options.backend = mode == "cpu" ? Backend::CPU : Backend::GPU;
  options.contextTokens = 2048;
  options.cacheDir = argc > 3 ? argv[3] : "";
  options.allowCpuFallback = true;
  std::string error;
  auto model = loadModel(argv[1], options, &error);
  require(model != nullptr, error.c_str());
  const auto info = model->runtimeInfo();
  const bool gpu = mode == "gpu";
  require(info.backend == (gpu ? Backend::GPU : Backend::CPU), "wrong actual backend");
  require(info.cpuFallback == (mode == "fallback"), "wrong fallback flag");
  require(!gpu || !info.device.empty(), "GPU device missing");
  require(mode != "fallback" || !info.fallbackReason.empty(), "fallback reason missing");
  std::cout << "RUNTIME " << (gpu ? "Vulkan" : "CPU") << " device=" << info.device
            << " fallback=" << info.cpuFallback << " reason=" << info.fallbackReason << std::endl;

  const std::string prefix = "<|turn>system\nReturn only a JSON object with one field action. No markdown.\n<turn|>\n";
  const std::string suffix = "<|turn>user\nAgreement: Boris will send the estimate on Friday. Extract the action.\n<turn|>\n<|turn>model\n";
  GenerateOptions generate;
  generate.maxOutputTokens = 128;
  std::string reply;
  GenerateStats stats;
  require(model->generate(prefix, suffix, generate, &reply, &stats) == Status::Ok, "JSON generation failed");
  const auto json = nlohmann::json::parse(reply);
  require(json.is_object() && json.contains("action") && json["action"].is_string(), "invalid action JSON");
  require(stats.decodeTokens > 2, "not a meaningful JSON response");
  std::cout << "JSON_OK tokens=" << stats.decodeTokens << " prefix_reused=" << stats.prefixReused << std::endl;
  reply.clear();
  require(model->generate(prefix, suffix, generate, &reply, &stats) == Status::Ok, "repeat generation failed");
  require(stats.prefixReused, "prefix checkpoint was not reused");
  require(nlohmann::json::parse(reply).is_object(), "repeat JSON invalid");
  std::cout << "PREFIX_REPEAT_OK" << std::endl;

  auto cancelled = std::async(std::launch::async, [&] {
    GenerateOptions longOutput;
    longOutput.maxOutputTokens = 384;
    longOutput.reusePrefix = false;
    std::string text;
    return model->generate("", "<|turn>user\nWrite a very long story about a ship, at least 2000 words.\n<turn|>\n<|turn>model\n", longOutput, &text);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (cancelled.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
    model->cancel();
    require(std::chrono::steady_clock::now() < deadline, "cancellation timed out");
  }
  require(cancelled.get() == Status::Cancelled, "inflight request was not cancelled");
  std::cout << "CANCEL_OK" << std::endl;
  reply.clear();
  require(model->generate(prefix, suffix, generate, &reply, &stats) == Status::Ok, "post-cancel recovery failed");
  require(nlohmann::json::parse(reply).is_object(), "post-cancel JSON invalid");
  std::cout << "RECOVERY_OK" << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << "SMOKE_FAILED " << e.what() << std::endl;
  return 1;
}
