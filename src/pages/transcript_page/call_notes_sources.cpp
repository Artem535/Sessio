#include "call_notes_sources.h"
#include "accent_color.h"
#include "call_notes_controller.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIconEngine>
#include <QJsonArray>
#include <QMenu>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <algorithm>
#include <oclero/qlementine/style/QlementineStyle.hpp>

namespace {
QColor noteAccent() {
  if (auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style()))
    return style->theme().primaryColor;
  return pcm::widgets::accentColor();
}
QColor disabledForeground() {
  if (auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style()))
    return style->colorForTextRole(oclero::qlementine::TextRole::Default, oclero::qlementine::MouseState::Disabled);
  return QApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
}

class QuoteIconEngine final : public QIconEngine {
public:
  QIconEngine *clone() const override { return new QuoteIconEngine; }
  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
    const auto color = mode == QIcon::Disabled ? disabledForeground() : noteAccent();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);
    painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    QPainterPath path;
    for (const int x : {4, 14}) {
      path.addRoundedRect(QRectF(x, 6, 6, 7), 1, 1);
      path.moveTo(x + 6, 13); path.cubicTo(x + 6, 16, x + 3, 18, x, 18);
    }
    painter->drawPath(path);
    painter->restore();
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    QPixmap result(size); result.fill(Qt::transparent);
    QPainter painter(&result); paint(&painter, QRect(QPoint{}, size), mode, state);
    return result;
  }
};

QString timeText(qint64 ms) {
  ms = std::max<qint64>(0, ms);
  return QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
}
}

QPushButton *makeSourceLink(CallNotesController *controller, const QJsonObject &item, bool compact,
                            QWidget *parent, std::function<void(qint64)> onClick) {
  if (!controller) return nullptr;
  QList<std::pair<qint64, QJsonObject>> sources;
  QSet<qint64> seen;
  for (const auto &value : item["evidence_ids"].toArray()) {
    const auto id = value.toString(); const auto source = controller->source(id);
    bool validId = false; const auto phraseId = id.toLongLong(&validId);
    if (source.isEmpty() || !validId || phraseId <= 0 || seen.contains(phraseId)) continue;
    sources.append({phraseId, source}); seen.insert(phraseId);
  }
  if (sources.isEmpty()) return nullptr;
  std::sort(sources.begin(), sources.end(), [](const auto &left, const auto &right) {
    const auto l = left.second, r = right.second;
    if (l["transcript_id"] != r["transcript_id"])
      return l["transcript_id"].toInteger() < r["transcript_id"].toInteger();
    return l["start_ms"].toInteger() < r["start_ms"].toInteger();
  });
  const auto start = timeText(sources.first().second["start_ms"].toInteger());
  QString range = start;
  if (sources.first().second["transcript_id"] == sources.last().second["transcript_id"])
    range += QStringLiteral("–") + timeText(sources.last().second["end_ms"].toInteger());
  const auto caption = compact ? start
      : QCoreApplication::translate("CallNotesSources", "%1 · %n phrases", nullptr, static_cast<int>(sources.size())).arg(range);
  auto *jump = new QPushButton(caption, parent);
  jump->setProperty("compactSource", compact);
  jump->setAccessibleName(QCoreApplication::translate("CallNotesSources", "Source · %1").arg(caption));
  if (!compact) jump->setIcon(QIcon(new QuoteIconEngine));
  jump->setIconSize(QSize(16, 16));
  jump->setObjectName("callNotesSource"); jump->setFlat(true); jump->setAutoDefault(false);
  jump->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  auto font = jump->font(); font.setBold(false); jump->setFont(font);
  auto colors = jump->palette();
  colors.setColor(QPalette::ButtonText, pcm::widgets::accentColor(jump->palette()));
  jump->setPalette(colors);
  QStringList quotes;
  for (const auto &entry : sources)
    quotes.append(entry.second["speaker"].toString() + QStringLiteral(": ") + entry.second["text"].toString());
  const auto quote = quotes.join(QStringLiteral("\n\n"));
  jump->setToolTip(QStringLiteral("<qt>") + quote.toHtmlEscaped().replace('\n', QStringLiteral("<br/>")) + QStringLiteral("</qt>"));
  jump->setAccessibleDescription(quote);
  jump->setProperty("validPhraseId", true); jump->setEnabled(controller->sourcesCurrent());
  QObject::connect(jump, &QPushButton::clicked, jump, [onClick, id = sources.first().first] { if (onClick) onClick(id); });
  if (sources.size() > 1) {
    QPointer<CallNotesController> guard(controller);
    jump->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(jump, &QWidget::customContextMenuRequested, jump, [jump, sources, guard, onClick](const QPoint &position) {
      auto *menu = new QMenu(jump);
      QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
      for (const auto &entry : sources) {
        const auto text = timeText(entry.second["start_ms"].toInteger()) + QStringLiteral(" · ") +
            entry.second["speaker"].toString() + QStringLiteral(": ") + entry.second["text"].toString().left(80);
        auto *action = menu->addAction(text);
        QObject::connect(action, &QAction::triggered, menu, [guard, onClick, id = entry.first] {
          if (guard && guard->sourcesCurrent() && !guard->source(QString::number(id)).isEmpty() && onClick)
            onClick(id);
        });
      }
      menu->popup(jump->mapToGlobal(position));
    });
  }
  return jump;
}

QString summaryCoverage(CallNotesController *controller) {
  if (!controller) return {};
  const auto count = static_cast<int>(controller->draft()["evidence_ids"].toArray().size());
  if (count == 0) return {};
  if (controller->ended() && controller->finalStatus() == "ready")
    return QCoreApplication::translate("CallNotesSources", "whole meeting · %n phrases", nullptr, count);
  const auto time = timeText(controller->processedThroughMs());
  return QCoreApplication::translate("CallNotesSources", "through %1 · %n phrases", nullptr, count).arg(time);
}
