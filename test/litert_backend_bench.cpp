// CPU vs Vulkan benchmark for libsessio_litert: one binary, backend chosen by argv.
// Diagnostic only; not part of the CMake/CTest build. Build against the Vulkan runtime:
//   L=~/.cache/sessio-litert-lm-gpu/install/lib
//   g++ -std=c++20 -O2 -pthread -Isrc/call_prompter/litert test/litert_backend_bench.cpp \
//     -L$L -lsessio_litert -Wl,-rpath-link,$L -Wl,-rpath,$L -o litert_backend_bench
//   ./litert_backend_bench model.litertlm gpu|cpu CACHE_DIR src/call_prompter/prompts/notes.txt 3
#include "sessio_litert.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace sessio::litert;
using Clock = std::chrono::steady_clock;

static std::string readFile(const std::string &path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static std::string utterances(int count) {
  static const char *lines[] = {
      "Добрый день, давайте начнём с того, как прошла неделя.",
      "Неделя была тяжёлая, на работе дедлайн по отчёту, спал по пять часов.",
      "Вы говорили, что хотели попробовать вечерние прогулки. Получилось?",
      "Два раза из пяти. В остальные дни задерживался в офисе.",
      "Давайте договоримся о следующей встрече. Вторник в 18:00 подойдёт?",
      "Вторник, нет, поправка: в среду в 18:00, во вторник у меня командировка.",
      "Хорошо, среда 18:00. И я пришлю вам дневник наблюдений до пятницы.",
      "Спасибо. Я ещё думаю, не перейти ли на встречи раз в две недели, но пока не решил.",
  };
  std::string out = "[";
  for (int i = 0; i < count; ++i) {
    const bool client = i % 2 == 1;
    if (i) out += ",";
    out += "{\"id\":\"u" + std::to_string(i + 1) + "\",\"speaker\":\"" + (client ? "Клиент" : "Специалист") +
           "\",\"text\":\"" + lines[i % 8] + "\",\"start_ms\":" + std::to_string(i * 9000) +
           ",\"end_ms\":" + std::to_string(i * 9000 + 8000) + "}";
  }
  return out + "]";
}

struct Scenario {
  std::string name, prefix, suffix;
  int maxOut;
};

int main(int argc, char **argv) {
  if (argc < 5) {
    std::cerr << "usage: bench model cpu|gpu cacheDir notes.txt [runs]\n";
    return 2;
  }
  const std::string mode = argv[2];
  const int runs = argc > 5 ? std::stoi(argv[5]) : 3;
  ModelOptions options;
  options.backend = mode == "gpu" ? Backend::GPU : Backend::CPU;
  options.allowCpuFallback = false;
  options.threads = 4;
  options.contextTokens = 4096;
  options.cacheDir = argv[3];

  const auto t0 = Clock::now();
  std::string error;
  auto model = loadModel(argv[1], options, &error);
  const double initSec = std::chrono::duration<double>(Clock::now() - t0).count();
  if (!model) {
    std::cerr << "LOAD_FAILED " << error << "\n";
    return 1;
  }
  const auto info = model->runtimeInfo();
  std::printf("BACKEND %s device=%s init_s=%.2f\n", info.backend == Backend::GPU ? "Vulkan" : "CPU",
              info.device.c_str(), initSec);

  const std::string notes = readFile(argv[4]);
  const std::string sys = "<|turn>system\n" + notes + "\n<turn|>\n";
  std::vector<Scenario> scenarios = {
      {"extract_short",
       "<|turn>system\nReturn only a JSON object with one field action. No markdown.\n<turn|>\n",
       "<|turn>user\nAgreement: Boris will send the estimate on Friday. Extract the action.\n<turn|>\n<|turn>model\n", 128},
      {"notes_8utt", sys, "<|turn>user\n" + utterances(8) + "\n<turn|>\n<|turn>model\n", 384},
      {"notes_36utt", sys, "<|turn>user\n" + utterances(36) + "\n<turn|>\n<|turn>model\n", 384},
  };

  for (const auto &s : scenarios) {
    for (int r = 0; r < runs; ++r) {
      GenerateOptions g;
      g.maxOutputTokens = s.maxOut;
      g.reusePrefix = false;  // cold prefill each run for comparable numbers
      std::string reply;
      GenerateStats st;
      const auto a = Clock::now();
      const auto status = model->generate(s.prefix, s.suffix, g, &reply, &st);
      const double wall = std::chrono::duration<double>(Clock::now() - a).count();
      std::printf("RUN %s #%d status=%d wall_s=%.2f ttft_s=%.2f prefill_tok=%d prefill_tps=%.1f decode_tok=%d decode_tps=%.1f\n",
                  s.name.c_str(), r + 1, static_cast<int>(status), wall, st.prefillSeconds, st.prefillTokens,
                  st.prefillSeconds > 0 ? st.prefillTokens / st.prefillSeconds : 0.0, st.decodeTokens,
                  st.decodeSeconds > 0 ? st.decodeTokens / st.decodeSeconds : 0.0);
      if (r == 0) std::printf("REPLY %s %s\n", s.name.c_str(), reply.c_str());
      std::fflush(stdout);
    }
  }
  return 0;
}
