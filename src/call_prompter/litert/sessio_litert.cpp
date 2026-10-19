#include "sessio_litert.h"
#include "task_gate.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "runtime/engine/engine.h"
#include "runtime/engine/engine_factory.h"
#include "runtime/engine/engine_settings.h"
#include "runtime/engine/io_types.h"
#include "runtime/executor/executor_settings_base.h"
#include "runtime/executor/llm_executor_settings.h"
#include "runtime/proto/sampler_params.pb.h"
#include "runtime/util/logging.h"

namespace sessio::litert {
namespace {

using ::litert::lm::Engine;
using ::litert::lm::EngineSettings;
using detail::TaskGate;

class AsyncTask final : public TaskGate::Task {
public:
  explicit AsyncTask(std::unique_ptr<Engine::Session::TaskController> controller)
      : mController(std::move(controller)) {}
  bool wait() override { return mController->WaitUntilDone(Engine::kDefaultTimeout).ok(); }
  void cancel() override { mController->Cancel().IgnoreError(); }
private:
  std::unique_ptr<Engine::Session::TaskController> mController;
};

Status taskStatus(TaskGate::Result result) {
  if (result == TaskGate::Result::Done) return Status::Ok;
  return result == TaskGate::Result::Cancelled ? Status::Cancelled : Status::Failed;
}

class LiteRtModel final : public Model {
public:
  LiteRtModel(std::unique_ptr<Engine> engine, int contextTokens, bool speculative)
      : mEngine(std::move(engine)), mContextTokens(contextTokens), mSpeculative(speculative) {}

  ~LiteRtModel() override { cancel(); }

  Status generate(const std::string &prefix, const std::string &suffix, const GenerateOptions &options,
                  std::string *reply, GenerateStats *stats) override {
    if (!mTasks.beginGeneration()) return Status::Cancelled;
    GenerateStats local;
    if (!stats) stats = &local;
    *stats = {};
    // Encoding does not mutate the tokenizer; the API only lacks const.
    auto &tokenizer = const_cast<::litert::support::Tokenizer &>(mEngine->GetTokenizer());
    const auto tokens = tokenizer.TextToTokenIds(prefix + suffix);
    if (!tokens.ok()) return Status::Failed;
    if (static_cast<int>(tokens->size()) + options.maxOutputTokens > mContextTokens) return Status::ContextExceeded;

    if (!options.reusePrefix) {
      auto session = createSession(options);
      if (!session) return Status::Failed;
      return complete(*session, prefix + suffix, options, reply, stats);
    }
    Status failure = Status::Failed;
    auto *cached = prefixSession(prefix, options, stats, &failure);
    if (!cached) return failure;
    const auto status = complete(*cached->session, suffix, options, reply, stats);
    // A cancelled or failed session is not reusable: drop its cached prefix.
    if (status == Status::Cancelled || status == Status::Failed) forget(prefix);
    return status;
  }

  void cancel() override { mTasks.cancel(); }

private:
  struct Prefix {
    std::string text;
    std::unique_ptr<Engine::Session> session;
  };
  static constexpr const char *kCheckpoint = "sessio-prefix";
  static constexpr size_t kMaxPrefixes = 3; // extraction, matching, deadline prompts

  std::unique_ptr<Engine::Session> createSession(const GenerateOptions &options) {
    auto config = ::litert::lm::SessionConfig::CreateDefault();
    config.SetApplyPromptTemplateInSession(false);
    config.SetMaxOutputTokens(options.maxOutputTokens);
    // LiteRT-LM v0.18.0 cannot resize the speculative drafter's KV cache after
    // RewindToCheckpoint ("Cannot auto-resize tensor kv_cache_v_N"), so prefix
    // reuse decodes without it. Greedy outputs were identical either way on the
    // smoke scenarios; Session::Clone() was not, so it is not used.
    config.SetEnableSpeculativeDecoding(mSpeculative && !options.reusePrefix);
    // Same sampler as the validated Python runtime: top-p type with k=1 and
    // temperature 0, which is greedy.
    auto &sampler = config.GetMutableSamplerParams();
    sampler.set_type(::litert::lm::proto::SamplerParameters::TOP_P);
    sampler.set_k(1);
    sampler.set_p(0.95f);
    sampler.set_temperature(0.0f);
    sampler.set_seed(options.seed);
    auto created = mEngine->CreateSession(config);
    return created.ok() ? std::move(*created) : nullptr;
  }

  // The session that has processed exactly `text`, rewound to that point.
  Prefix *prefixSession(const std::string &text, const GenerateOptions &options, GenerateStats *stats,
                        Status *failure) {
    for (auto it = mPrefixes.begin(); it != mPrefixes.end(); ++it) {
      if (it->text != text) continue;
      if (!it->session->RewindToCheckpoint(kCheckpoint).ok()) {
        mPrefixes.erase(it);
        break;
      }
      std::rotate(mPrefixes.begin(), it, it + 1); // most recently used first
      stats->prefixReused = true;
      return &mPrefixes.front();
    }
    auto session = createSession(options);
    if (!session) return nullptr;
    const auto before = prefillTurns(*session);
    if (const auto status = prefill(*session, text); status != Status::Ok) {
      *failure = status;
      return nullptr;
    }
    if (!session->SaveCheckpoint(kCheckpoint).ok()) return nullptr;
    addTurns(*session, before, stats);
    if (mPrefixes.size() >= kMaxPrefixes) mPrefixes.pop_back();
    mPrefixes.insert(mPrefixes.begin(), Prefix{text, std::move(session)});
    return &mPrefixes.front();
  }

  void forget(const std::string &text) {
    std::erase_if(mPrefixes, [&text](const Prefix &p) { return p.text == text; });
  }

  static uint64_t prefillTurns(Engine::Session &session) {
    const auto info = session.GetBenchmarkInfo();
    return info.ok() ? info->GetTotalPrefillTurns() : 0;
  }

  static void addTurns(Engine::Session &session, uint64_t fromTurn, GenerateStats *stats) {
    const auto info = session.GetBenchmarkInfo();
    if (!info.ok()) return;
    for (auto i = fromTurn; i < info->GetTotalPrefillTurns(); ++i)
      if (const auto turn = info->GetPrefillTurn(static_cast<int>(i)); turn.ok()) {
        stats->prefillTokens += static_cast<int>(turn->num_tokens);
        stats->prefillSeconds += absl::ToDoubleSeconds(turn->duration);
      }
  }

  Status prefill(Engine::Session &session, const std::string &text) {
    std::vector<::litert::lm::InputData> inputs;
    inputs.emplace_back(::litert::lm::InputText(text));
    auto prefillOk = std::make_shared<std::atomic_bool>(true);
    const auto status = mTasks.run([&]() -> std::shared_ptr<TaskGate::Task> {
      auto task = session.RunPrefillAsync(inputs, [prefillOk](auto response) {
        if (!response.ok()) *prefillOk = false;
      });
      if (!task.ok()) return {};
      return std::make_shared<AsyncTask>(std::move(*task));
    });
    if (status != TaskGate::Result::Done) return taskStatus(status);
    return *prefillOk ? Status::Ok : Status::Failed;
  }

  // Prefills `text` after whatever the session already holds, then decodes.
  Status complete(Engine::Session &session, const std::string &text, const GenerateOptions &options,
                  std::string *reply, GenerateStats *stats) {
    const auto before = prefillTurns(session);
    if (const auto status = prefill(session, text); status != Status::Ok) return status;
    addTurns(session, before, stats);

    struct Output {
      std::mutex mutex;
      std::string text;
      bool ok = true, done = false;
    };
    auto output = std::make_shared<Output>();
    const auto status = mTasks.run([&]() -> std::shared_ptr<TaskGate::Task> {
      auto task = session.RunDecodeAsync([output](auto response) {
        std::lock_guard lock(output->mutex);
        if (!response.ok() || response->GetTexts().size() > 1) { output->ok = false; return; }
        if (!response->GetTexts().empty()) output->text += response->GetTexts().front();
        if (response->GetTaskState() == ::litert::lm::TaskState::kDone) output->done = true;
      });
      if (!task.ok()) return {};
      return std::make_shared<AsyncTask>(std::move(*task));
    });
    if (status != TaskGate::Result::Done) return taskStatus(status);
    {
      std::lock_guard lock(output->mutex);
      if (!output->ok || !output->done || output->text.empty()) return Status::Failed;
      *reply = std::move(output->text);
    }
    const auto benchmark = session.GetBenchmarkInfo();
    if (!benchmark.ok() || benchmark->GetTotalDecodeTurns() == 0) return Status::Failed;
    const auto turn = benchmark->GetDecodeTurn(static_cast<int>(benchmark->GetTotalDecodeTurns() - 1));
    if (!turn.ok()) return Status::Failed;
    stats->decodeTokens = static_cast<int>(turn->num_tokens);
    stats->decodeSeconds = absl::ToDoubleSeconds(turn->duration);
    if (turn->num_tokens >= static_cast<uint64_t>(options.maxOutputTokens)) return Status::Truncated;
    return Status::Ok;
  }

  std::unique_ptr<Engine> mEngine;
  const int mContextTokens;
  const bool mSpeculative;
  TaskGate mTasks;
  std::vector<Prefix> mPrefixes;
};

} // namespace

Model::~Model() = default;

std::unique_ptr<Model> loadModel(const std::string &path, const ModelOptions &options, std::string *error) {
  const auto fail = [error](const char *reason) -> std::unique_ptr<Model> {
    if (error) *error = reason;
    return nullptr;
  };
  // Runtime diagnostics may quote prompts; keep errors only.
  ::litert::lm::SetMinLogSeverity(::litert::lm::LogSeverity::kError);
  auto assets = ::litert::lm::ModelAssets::Create(path);
  if (!assets.ok()) return fail("model file cannot be opened");
  auto settings = EngineSettings::CreateDefault(std::move(*assets), ::litert::lm::Backend::CPU);
  if (!settings.ok()) return fail("model settings are invalid");
  auto &main = settings->GetMutableMainExecutorSettings();
  main.SetMaxNumTokens(options.contextTokens);
  std::error_code ignored;
  if (!options.cacheDir.empty() && (std::filesystem::create_directories(options.cacheDir, ignored) ||
                                    std::filesystem::is_directory(options.cacheDir, ignored)))
    main.SetCacheDir(options.cacheDir);
  if (auto cpu = main.MutableBackendConfig<::litert::lm::CpuConfig>(); cpu.ok()) {
    auto config = *cpu;
    config.number_of_threads = options.threads;
    main.SetBackendConfig(config);
  }
  auto advanced = main.GetAdvancedSettings().value_or(::litert::lm::AdvancedSettings());
  advanced.enable_speculative_decoding = options.speculativeDecoding;
  main.SetAdvancedSettings(advanced);
  settings->GetMutableBenchmarkParams(); // enables the per-turn decode token count
  auto engine = ::litert::lm::EngineFactory::CreateDefault(std::move(*settings));
  if (!engine.ok()) return fail("model cannot be loaded");
  return std::make_unique<LiteRtModel>(std::move(*engine), options.contextTokens, options.speculativeDecoding);
}

} // namespace sessio::litert
