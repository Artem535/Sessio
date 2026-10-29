#include "sessio_litert.h"
#include "task_gate.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

#ifdef SESSIO_LITERT_VULKAN
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#endif
#ifdef SESSIO_LITERT_DXGI
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#endif

#include "absl/time/time.h"
#include "runtime/components/constrained_decoding/constraint.h"
#include "runtime/components/constrained_decoding/constraint_provider.h"
#include "runtime/components/constrained_decoding/constraint_provider_factory.h"
#include "runtime/components/constrained_decoding/llg_constraint_config.h"
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

// Pin WebGPU to an enumerated hardware adapter. In particular, a machine
// exposing only a software adapter (llvmpipe, Microsoft Basic Render Driver)
// must take the explicit CPU fallback path. The result is matched as a
// substring of the WebGPU adapter name.
std::string gpuDevice() {
#if defined(SESSIO_LITERT_DXGI)
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) return {};
  struct FactoryScope { IDXGIFactory1 *factory; ~FactoryScope() { factory->Release(); } } factoryScope{factory};
  std::wstring best;
  SIZE_T bestMemory = 0;
  IDXGIAdapter1 *adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    const bool described = SUCCEEDED(adapter->GetDesc1(&desc));
    adapter->Release();
    constexpr UINT kMicrosoftVendor = 0x1414; // Basic Render Driver / WARP
    if (!described || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || desc.VendorId == kMicrosoftVendor) continue;
    // Discrete adapters have the most dedicated memory.
    if (best.empty() || desc.DedicatedVideoMemory > bestMemory) {
      best = desc.Description;
      bestMemory = desc.DedicatedVideoMemory;
    }
  }
  if (best.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, best.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string name(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, best.c_str(), -1, name.data(), size, nullptr, nullptr);
  return name;
#elif defined(SESSIO_LITERT_APPLE_GPU)
  // Apple silicon always has its GPU; Metal adapters are named "Apple M…".
  return "Apple";
#elif defined(SESSIO_LITERT_VULKAN)
  void *library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!library) return {};
  struct LibraryScope { void *handle; ~LibraryScope() { dlclose(handle); } } scope{library};
  const auto create = reinterpret_cast<PFN_vkCreateInstance>(dlsym(library, "vkCreateInstance"));
  const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(dlsym(library, "vkDestroyInstance"));
  const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(dlsym(library, "vkEnumeratePhysicalDevices"));
  const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(dlsym(library, "vkGetPhysicalDeviceProperties"));
  if (!create || !destroy || !enumerate || !properties) return {};
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "Sessio";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo config{};
  config.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  config.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (create(&config, nullptr, &instance) != VK_SUCCESS) return {};
  struct InstanceScope {
    VkInstance instance; PFN_vkDestroyInstance destroy;
    ~InstanceScope() { destroy(instance, nullptr); }
  } instanceScope{instance, destroy};
  uint32_t count = 0;
  if (enumerate(instance, &count, nullptr) != VK_SUCCESS || !count) return {};
  std::vector<VkPhysicalDevice> devices(count);
  if (enumerate(instance, &count, devices.data()) != VK_SUCCESS) return {};
  std::string integrated;
  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties info{};
    properties(devices[i], &info);
    if (info.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) return info.deviceName;
    if (info.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && integrated.empty()) integrated = info.deviceName;
  }
  return integrated;
#else
  return {};
#endif
}

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
  LiteRtModel(std::unique_ptr<Engine> engine, int contextTokens, bool speculative, RuntimeInfo info)
      : mEngine(std::move(engine)), mContextTokens(contextTokens), mSpeculative(speculative), mInfo(std::move(info)) {}

  ~LiteRtModel() override { cancel(); }

  Status generate(const std::string &prefix, const std::string &suffix, const GenerateOptions &options,
                  std::string *reply, GenerateStats *stats) override {
    if (!mTasks.beginGeneration()) return Status::Cancelled;
    struct GenerationScope {
      TaskGate &gate;
      ~GenerationScope() { gate.endGeneration(); }
    } scope{mTasks};
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
  RuntimeInfo runtimeInfo() const override { return mInfo; }

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
    auto decode = ::litert::lm::DecodeConfig::CreateDefault();
    // DecodeConfig does not own the constraint; the callback keeps it alive
    // for as long as the decode task can still call it.
    std::shared_ptr<::litert::lm::Constraint> constraint;
    if (!options.jsonSchema.empty()) constraint = jsonConstraint(session, options.jsonSchema);
    stats->constrained = constraint != nullptr;
    if (constraint) decode.SetConstraint(constraint.get());
    const auto status = mTasks.run([&]() -> std::shared_ptr<TaskGate::Task> {
      auto task = session.RunDecodeAsync([output, constraint](auto response) {
        std::lock_guard lock(output->mutex);
        if (!response.ok() || response->GetTexts().size() > 1) { output->ok = false; return; }
        if (!response->GetTexts().empty()) output->text += response->GetTexts().front();
        if (response->GetTaskState() == ::litert::lm::TaskState::kDone) output->done = true;
      }, decode);
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

  // Null when llguidance cannot be set up or rejects the schema: the reply is
  // then decoded unconstrained and the caller's validation still applies.
  std::shared_ptr<::litert::lm::Constraint> jsonConstraint(Engine::Session &session, const std::string &schema) {
    if (!mConstraints) {
      auto provider = ::litert::lm::CreateConstraintProvider(::litert::lm::LlGuidanceConfig(), mEngine->GetTokenizer(),
                                                             session.GetSessionConfig().GetStopTokenIds());
      if (!provider.ok()) return nullptr;
      mConstraints = std::move(*provider);
    }
    auto constraint = mConstraints->CreateConstraint(::litert::lm::LlGuidanceConstraintArg{
        .constraint_type = ::litert::lm::LlgConstraintType::kJsonSchema, .constraint_string = schema});
    if (!constraint.ok()) return nullptr;
    return std::shared_ptr<::litert::lm::Constraint>(std::move(*constraint));
  }

  std::unique_ptr<Engine> mEngine;
  std::unique_ptr<::litert::lm::ConstraintProvider> mConstraints;
  const int mContextTokens;
  const bool mSpeculative;
  const RuntimeInfo mInfo;
  TaskGate mTasks;
  std::vector<Prefix> mPrefixes;
};

} // namespace

Model::~Model() = default;

std::unique_ptr<Model> loadModel(const std::string &path, const ModelOptions &options, std::string *error) {
  // Runtime diagnostics may quote prompts; keep errors only.
  ::litert::lm::SetMinLogSeverity(::litert::lm::LogSeverity::kError);
  if (error) error->clear();
  std::string reason;
  const auto attempt = [&](Backend backend, const std::string &fallbackReason) -> std::unique_ptr<Model> {
    const auto fail = [&](const char *text) -> std::unique_ptr<Model> { reason = text; return nullptr; };
    RuntimeInfo info;
    info.backend = backend;
    info.cpuFallback = !fallbackReason.empty();
    info.fallbackReason = fallbackReason;
    if (backend == Backend::GPU) {
      info.device = gpuDevice();
      if (info.device.empty()) return fail("hardware GPU device unavailable");
    }
    auto assets = ::litert::lm::ModelAssets::Create(path);
    if (!assets.ok()) return fail("model file cannot be opened");
    const auto requested = backend == Backend::GPU ? ::litert::lm::Backend::GPU : ::litert::lm::Backend::CPU;
    auto settings = EngineSettings::CreateDefault(std::move(*assets), requested,
        ::litert::lm::Backend::CPU, ::litert::lm::Backend::CPU, ::litert::lm::Backend::CPU);
    if (!settings.ok()) return fail("model settings are invalid");
    auto &main = settings->GetMutableMainExecutorSettings();
    main.SetMaxNumTokens(options.contextTokens);
    if (!options.cacheDir.empty()) {
      std::error_code ignored;
      const auto cache = std::filesystem::path(options.cacheDir) / (backend == Backend::GPU ? "vulkan" : "cpu");
      std::filesystem::create_directories(cache, ignored);
      if (ignored) return fail("runtime cache directory unavailable");
      main.SetCacheDir(cache.string());
    }
    if (auto cpu = main.MutableBackendConfig<::litert::lm::CpuConfig>(); cpu.ok()) {
      auto config = *cpu;
      config.number_of_threads = options.threads;
      main.SetBackendConfig(config);
    }
    auto advanced = main.GetAdvancedSettings().value_or(::litert::lm::AdvancedSettings());
    advanced.enable_speculative_decoding = options.speculativeDecoding;
    advanced.preferred_device_substr = info.device;
    main.SetAdvancedSettings(advanced);
    settings->GetMutableBenchmarkParams();
    auto engine = ::litert::lm::EngineFactory::CreateDefault(std::move(*settings));
    if (!engine.ok()) return fail(backend == Backend::GPU ? "GPU engine initialization failed" : "CPU engine initialization failed");
    if ((*engine)->GetEngineSettings().GetMainExecutorSettings().GetBackend() != requested)
      return fail("runtime backend does not match request");
    std::fprintf(stderr, "Sessio LiteRT: requested=%s actual=%s device=%s fallback=%s\n",
                 options.backend == Backend::GPU ? "GPU" : "CPU", backend == Backend::GPU ? "GPU" : "CPU",
                 info.device.c_str(), fallbackReason.empty() ? "none" : fallbackReason.c_str());
    return std::make_unique<LiteRtModel>(std::move(*engine), options.contextTokens, options.speculativeDecoding, std::move(info));
  };
  if (auto model = attempt(options.backend, {})) return model;
  if (options.backend == Backend::GPU && options.allowCpuFallback) {
    const auto gpuFailure = reason;
    if (auto model = attempt(Backend::CPU, gpuFailure)) return model;
  }
  if (error) *error = reason;
  return nullptr;
}

} // namespace sessio::litert
