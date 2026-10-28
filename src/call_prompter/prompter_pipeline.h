#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pcm::prompter {

// Raised for any request or model output the pipeline must not act on. The
// message is a fixed diagnostic and never contains source text or prompts.
class PipelineError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Groups a PipelineError message into what the user can act on:
// "invalid_output" (the model's reply was unusable), "too_long" (input over
// the model budget), "cancelled", or "runtime" (anything else).
QString failureReason(const QString &pipelineError);

// Top-level model input. Keys keep their insertion order, as the prompts were
// validated with that exact serialisation.
using Payload = std::vector<std::pair<QString, QJsonValue>>;

// One model call: system prompt plus serialised user payload in, parsed JSON
// value out. `schema` is a JSON schema for constrained decoding, or empty for
// free text. Throws PipelineError (or any std::exception) on failure.
using Generate = std::function<QJsonValue(const QString &system, const QByteArray &payload, const QByteArray &schema)>;

// Validates a protocol-1 request, extracts agreement events from its
// utterances, matches them against the candidate records and checks deadlines.
// Returns the protocol-1 reply object. Throws PipelineError on invalid input
// or invalid extraction output.
QJsonObject processRequest(const QJsonObject &request, const Generate &generate);

// Serialises like Python's json.dumps(value, ensure_ascii=False): ", " and
// ": " separators, non-ASCII kept as is. Objects that carry a "kind" key are
// events and keep the extraction schema order; others are alphabetical, which
// is the order of the compact Qt JSON the controller sends.
QByteArray dumpJson(const QJsonValue &value);
QByteArray dumpPayload(const Payload &payload);

// Parses a raw model reply, tolerating surrounding whitespace and a Markdown
// code fence. Throws PipelineError when the reply is not JSON.
QJsonValue parseModelReply(const QString &raw);

// Gemma 4 chat format for one system and one user turn, thinking disabled,
// without the leading <bos> (the engine adds it).
QString renderGemmaPrompt(const QString &system, const QString &user);
// The two halves of renderGemmaPrompt(). The system turn ends right before the
// <|turn> special token, so it can be processed once and its KV cache reused.
QString gemmaSystemTurn(const QString &system);
QString gemmaUserTurn(const QString &user);

} // namespace pcm::prompter
