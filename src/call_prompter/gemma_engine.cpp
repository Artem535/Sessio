#include "prompter_engine.h"
#include "prompter_pipeline.h"
#include "sessio_litert.h"

namespace pcm::prompter {
namespace {

constexpr qsizetype kMaxReplyChars = 48000;

class GemmaEngine final : public PrompterEngine {
public:
  explicit GemmaEngine(std::unique_ptr<sessio::litert::Model> model) : mModel(std::move(model)) {}

  QJsonValue generate(const QString &system, const QByteArray &payload) override {
    std::string reply;
    const auto prompt = renderGemmaPrompt(system, QString::fromUtf8(payload)).toStdString();
    switch (mModel->generate(prompt, {}, &reply)) {
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
};

} // namespace

bool gemmaRuntimeAvailable() { return true; }

std::unique_ptr<PrompterEngine> createGemmaEngine(const QString &modelPath, const QString &cacheDir) {
  sessio::litert::ModelOptions options;
  options.cacheDir = cacheDir.toStdString();
  auto model = sessio::litert::loadModel(modelPath.toStdString(), options, nullptr);
  if (!model) return nullptr;
  return std::make_unique<GemmaEngine>(std::move(model));
}

} // namespace pcm::prompter
