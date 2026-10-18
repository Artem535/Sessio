#include "sessio_litert.h"
#include "task_gate.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

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
  LiteRtModel(std::unique_ptr<Engine> engine, int contextTokens)
      : mEngine(std::move(engine)), mContextTokens(contextTokens) {}

  ~LiteRtModel() override { cancel(); }

  Status generate(const std::string &prompt, const GenerateOptions &options, std::string *reply) override {
    if (!mTasks.beginGeneration()) return Status::Cancelled;
    // Encoding does not mutate the tokenizer; the API only lacks const.
    auto &tokenizer = const_cast<::litert::support::Tokenizer &>(mEngine->GetTokenizer());
    const auto tokens = tokenizer.TextToTokenIds(prompt);
    if (!tokens.ok()) return Status::Failed;
    if (static_cast<int>(tokens->size()) + options.maxOutputTokens > mContextTokens) return Status::ContextExceeded;

    auto config = ::litert::lm::SessionConfig::CreateDefault();
    config.SetApplyPromptTemplateInSession(false);
    config.SetMaxOutputTokens(options.maxOutputTokens);
    config.SetEnableSpeculativeDecoding(true);
    // Same sampler as the validated Python runtime: top-p type with k=1 and
    // temperature 0, which is greedy.
    auto &sampler = config.GetMutableSamplerParams();
    sampler.set_type(::litert::lm::proto::SamplerParameters::TOP_P);
    sampler.set_k(1);
    sampler.set_p(0.95f);
    sampler.set_temperature(0.0f);
    sampler.set_seed(options.seed);
    auto created = mEngine->CreateSession(config);
    if (!created.ok()) return Status::Failed;
    std::unique_ptr<Engine::Session> session = std::move(*created);
    return run(*session, prompt, options, reply);
  }

  void cancel() override { mTasks.cancel(); }

private:
  Status run(Engine::Session &session, const std::string &prompt, const GenerateOptions &options,
                    std::string *reply) {
    std::vector<::litert::lm::InputData> inputs;
    inputs.emplace_back(::litert::lm::InputText(prompt));
    auto prefillOk = std::make_shared<std::atomic_bool>(true);
    auto status = mTasks.run([&]() -> std::shared_ptr<TaskGate::Task> {
      auto task = session.RunPrefillAsync(inputs, [prefillOk](auto response) {
        if (!response.ok()) *prefillOk = false;
      });
      if (!task.ok()) return {};
      return std::make_shared<AsyncTask>(std::move(*task));
    });
    if (status != TaskGate::Result::Done) return taskStatus(status);
    if (!*prefillOk) return Status::Failed;

    struct Output {
      std::mutex mutex;
      std::string text;
      bool ok = true, done = false;
    };
    auto output = std::make_shared<Output>();
    status = mTasks.run([&]() -> std::shared_ptr<TaskGate::Task> {
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
    if (turn->num_tokens >= static_cast<uint64_t>(options.maxOutputTokens)) return Status::Truncated;
    return Status::Ok;
  }

  std::unique_ptr<Engine> mEngine;
  const int mContextTokens;
  TaskGate mTasks;
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
  advanced.enable_speculative_decoding = true;
  main.SetAdvancedSettings(advanced);
  settings->GetMutableBenchmarkParams(); // enables the per-turn decode token count
  auto engine = ::litert::lm::EngineFactory::CreateDefault(std::move(*settings));
  if (!engine.ok()) return fail("model cannot be loaded");
  return std::make_unique<LiteRtModel>(std::move(*engine), options.contextTokens);
}

} // namespace sessio::litert
