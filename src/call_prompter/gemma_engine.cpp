#include "prompter_engine.h"
#include "prompter_pipeline.h"
#include "sessio_litert.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logPrompterModel, "pcm.prompter.model")

namespace pcm::prompter {
namespace {

constexpr qsizetype kMaxReplyChars = 48000;
constexpr int kSchemaOutputTokens = 4096;

class GemmaEngine final : public PrompterEngine {
public:
  GemmaEngine(std::unique_ptr<sessio::litert::Model> model, bool reusePrefix, bool qwen)
      : mModel(std::move(model)), mReusePrefix(reusePrefix), mQwen(qwen) {}

  QJsonValue generate(const QString &system, const QByteArray &payload, const QByteArray &schema) override {
    std::string reply;
    sessio::litert::GenerateOptions options;
    options.reusePrefix = mReusePrefix;
    options.jsonSchema = schema.toStdString();
    // A schema bounds the reply by its own lengths; the meeting summary's
    // overview needs more than the default 384 tokens. Pipelines budget their
    // input for this reserve (notes::kContextInputBytes).
    if (!schema.isEmpty()) options.maxOutputTokens = kSchemaOutputTokens;
    sessio::litert::GenerateStats stats;
    QElapsedTimer timer;
    timer.start();
    const auto systemTurn = mQwen ? qwenSystemTurn(system) : gemmaSystemTurn(system);
    const auto userTurn = mQwen ? qwenUserTurn(QString::fromUtf8(payload)) : gemmaUserTurn(QString::fromUtf8(payload));
    const auto status = mModel->generate(systemTurn.toStdString(),
                                         userTurn.toStdString(), options, &reply,
                                         &stats);
    // Counts and timings only: never prompt or reply text.
    qCInfo(logPrompterModel).nospace()
        << "generate " << timer.elapsed() << " ms: prefill " << stats.prefillTokens << " tokens "
        << qRound(stats.prefillSeconds * 1000) << " ms" << (stats.prefixReused ? " (prefix reused)" : "")
        << (schema.isEmpty() ? "" : stats.constrained ? " (schema)" : " (schema unavailable)")
        << ", decode " << stats.decodeTokens << " tokens " << qRound(stats.decodeSeconds * 1000) << " ms";
    switch (status) {
    case sessio::litert::Status::Ok: break;
    case sessio::litert::Status::Cancelled: throw PipelineError("cancelled");
    case sessio::litert::Status::ContextExceeded: throw PipelineError("context budget exceeded");
    case sessio::litert::Status::Truncated: throw PipelineError("output truncated");
    case sessio::litert::Status::Failed: throw PipelineError("inference failed");
    }
    const auto text = QString::fromStdString(reply);
    if (text.size() > kMaxReplyChars) throw PipelineError("output budget exceeded");
    return parseModelReply(text);
  }

  void cancel() override { mModel->cancel(); }

private:
  std::unique_ptr<sessio::litert::Model> mModel;
  const bool mReusePrefix;
  const bool mQwen;
};

} // namespace

bool gemmaRuntimeAvailable() { return true; }

std::unique_ptr<PrompterEngine> createGemmaEngine(const QString &modelPath, const QString &cacheDir,
                                                  const GemmaOptions &gemma) {
  sessio::litert::ModelOptions options;
  const bool qwen = isQwenLitertModel(modelPath);
  options.cacheDir = cacheDir.toStdString();
  options.speculativeDecoding = !gemma.reusePrefix;
  options.backend = gemma.acceleration == GemmaOptions::Acceleration::Cpu ? sessio::litert::Backend::CPU
                                                                           : sessio::litert::Backend::GPU;
  // Current Qwen3.5 LiteRT graphs can initialize on GPU but fail during
  // inference. Auto uses the verified CPU path; an explicit GPU selection is
  // still respected and may fail if its runtime cannot execute the graph.
  if (qwen && gemma.acceleration == GemmaOptions::Acceleration::Auto)
    options.backend = sessio::litert::Backend::CPU;
  options.allowCpuFallback = gemma.acceleration == GemmaOptions::Acceleration::Auto;
  auto model = sessio::litert::loadModel(modelPath.toStdString(), options, nullptr);
  if (!model) return nullptr;
  return std::make_unique<GemmaEngine>(std::move(model), gemma.reusePrefix, qwen);
}

} // namespace pcm::prompter
