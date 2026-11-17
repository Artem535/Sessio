#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

namespace pcm::prompter {
struct SummaryLimits {
  static constexpr int kOverviewMax = 6000;
  static constexpr int kLeafMax = 2400;
  int overview = kOverviewMax;
  int leaf = kLeafMax;
  int points = 20;
  int tasks = 12;
  int questions = 10;
  // "parts": skip the final model call and join the chunk notes instead.
  bool partsOnly = false;

  QJsonObject json() const {
    return {{"overview", overview}, {"leaf", leaf}, {"points", points}, {"tasks", tasks}, {"questions", questions},
            {"partsOnly", partsOnly}};
  }
  static SummaryLimits fromJson(const QJsonObject &obj) {
    SummaryLimits d;
    d.overview = qBound(500, obj["overview"].toInt(d.overview), kOverviewMax);
    d.leaf = qBound(100, obj["leaf"].toInt(d.leaf), kLeafMax);
    d.points = qBound(2, obj["points"].toInt(d.points), 50);
    d.tasks = qBound(2, obj["tasks"].toInt(d.tasks), 30);
    d.questions = qBound(2, obj["questions"].toInt(d.questions), 30);
    d.partsOnly = obj["partsOnly"].toBool(false);
    return d;
  }
  static SummaryLimits settings() {
    QSettings s;
    QJsonObject obj;
    for (const char *key : {"overview", "leaf", "points", "tasks", "questions"}) {
      const auto value = s.value(QStringLiteral("summary/") + key);
      if (value.isValid()) obj[key] = value.toInt();
    }
    auto d = fromJson(obj);
    d.partsOnly = s.value(QStringLiteral("summary/mode")).toString() == QLatin1String("parts");
    return d;
  }
};
} // namespace pcm::prompter
