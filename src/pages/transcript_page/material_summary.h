#pragma once
#include <QJsonObject>
#include <QString>

// Text of a call material's meeting summary: the format-2 overview, else the
// note of a summary saved before format 2. Empty without a summary or when it
// was dismissed; live notes have no summary. Header-only: client notes read
// call materials without linking the transcript page.
inline QString materialSummaryText(const QJsonObject &state) {
  const auto final = state["final"].toObject();
  if (final.isEmpty() || final["dismissed"].toBool()) return {};
  return final[final["format"].toInt() == 2 ? "overview" : "note"].toObject()["text"].toString().trimmed();
}
