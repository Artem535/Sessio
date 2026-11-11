#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

namespace pcm::prompter {
struct SummaryLimits {
  int overview = 3000;
  int leaf = 400;
  int points = 20;
  int tasks = 12;
  int questions = 10;

  QJsonObject json() const {
    return {{"overview", overview}, {"leaf", leaf}, {"points", points}, {"tasks", tasks}, {"questions", questions}};
  }
  static SummaryLimits fromJson(const QJsonObject &obj) {
    SummaryLimits d;
    d.overview = qBound(500, obj["overview"].toInt(d.overview), 6000);
    d.leaf = qBound(100, obj["leaf"].toInt(d.leaf), 800);
    d.points = qBound(2, obj["points"].toInt(d.points), 50);
    d.tasks = qBound(2, obj["tasks"].toInt(d.tasks), 30);
    d.questions = qBound(2, obj["questions"].toInt(d.questions), 30);
    return d;
  }
  static SummaryLimits settings() {
    QSettings s;
    QJsonObject obj;
    for (const char *key : {"overview", "leaf", "points", "tasks", "questions"}) {
      const auto value = s.value(QStringLiteral("summary/") + key);
      if (value.isValid()) obj[key] = value.toInt();
    }
    return fromJson(obj);
  }
};
} // namespace pcm::prompter
