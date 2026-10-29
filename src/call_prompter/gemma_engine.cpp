#include "prompter_engine.h"
#include "prompter_pipeline.h"
#include "sessio_litert.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logPrompterModel, "pcm.prompter.model")

namespace pcm::prompter {
namespace {

constexpr qsizetype kMaxReplyChars = 48000;

class GemmaEngine final : public PrompterEngine {
public:
  GemmaEngine(std::unique_ptr<sessio::litert::Model> model, bool reusePrefix)
      : mModel(std::move(model)), mReusePrefix(reusePrefix) {}

  QJsonValue generate(const QString &system, const QByteArray &payload, const QByteArray &schema) override {
    std::string reply;
    sessio::litert::GenerateOptions options;
    options.reusePrefix = mReusePrefix;
    options.jsonSchema = schema.toStdString();
    sessio::litert::GenerateStats stats;
    QElapsedTimer timer;
    timer.start();
    const auto status = mModel->generate(gemmaSystemTurn(system).toStdString(),
                                         gemmaUserTurn(QString::fromUtf8(payload)).toStdString(), options, &reply,
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
};

} // namespace

bool gemmaRuntimeAvailable() { return true; }

std::unique_ptr<PrompterEngine> createGemmaEngine(const QString &modelPath, const QString &cacheDir,
                                                  const GemmaOptions &gemma) {
  sessio::litert::ModelOptions options;
  options.cacheDir = cacheDir.toStdString();
  options.speculativeDecoding = !gemma.reusePrefix;
  options.backend = gemma.acceleration == GemmaOptions::Acceleration::Cpu ? sessio::litert::Backend::CPU
                                                                           : sessio::litert::Backend::GPU;
  options.allowCpuFallback = gemma.acceleration == GemmaOptions::Acceleration::Auto;
  auto model = sessio::litert::loadModel(modelPath.toStdString(), options, nullptr);
  if (!model) return nullptr;
  return std::make_unique<GemmaEngine>(std::move(model), gemma.reusePrefix);
}

} // namespace pcm::prompter
