#include "sessio_litert.h"

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

class LiteRtModel final : public Model {
public:
  LiteRtModel(std::unique_ptr<Engine> engine, int contextTokens)
      : mEngine(std::move(engine)), mContextTokens(contextTokens) {}

  ~LiteRtModel() override { cancel(); }

  Status generate(const std::string &prompt, const GenerateOptions &options, std::string *reply) override {
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
    {
      std::lock_guard lock(mMutex);
      if (mCancelled.exchange(false)) return Status::Cancelled;
      mSession = session.get();
    }
    const auto status = run(*session, prompt, options, reply);
    std::lock_guard lock(mMutex);
    mSession = nullptr;
    return mCancelled.exchange(false) ? Status::Cancelled : status;
  }

  void cancel() override {
    std::lock_guard lock(mMutex);
    mCancelled = true;
    if (mSession) mSession->CancelProcess();
  }

private:
  static Status run(Engine::Session &session, const std::string &prompt, const GenerateOptions &options,
                    std::string *reply) {
    std::vector<::litert::lm::InputData> inputs;
    inputs.emplace_back(::litert::lm::InputText(prompt));
    if (!session.RunPrefill(inputs).ok()) return Status::Failed;
    const auto responses = session.RunDecode();
    if (!responses.ok() || responses->GetTexts().empty()) return Status::Failed;
    const auto benchmark = session.GetBenchmarkInfo();
    if (!benchmark.ok() || benchmark->GetTotalDecodeTurns() == 0) return Status::Failed;
    const auto turn = benchmark->GetDecodeTurn(static_cast<int>(benchmark->GetTotalDecodeTurns() - 1));
    if (!turn.ok()) return Status::Failed;
    if (turn->num_tokens >= static_cast<uint64_t>(options.maxOutputTokens)) return Status::Truncated;
    *reply = responses->GetTexts().front();
    return Status::Ok;
  }

  std::unique_ptr<Engine> mEngine;
  const int mContextTokens;
  std::mutex mMutex;
  Engine::Session *mSession = nullptr;
  std::atomic_bool mCancelled = false;
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
