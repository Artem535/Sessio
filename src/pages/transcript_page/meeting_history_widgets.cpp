#include "meeting_history_widgets.h"
#include "agreements_widget.h"
#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "key_points_widget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QString withDuration(const QString &date, qint64 durationMs) {
  if (durationMs <= 0) return date;
  return QCoreApplication::translate("MeetingHistory", "%1 · %2 min").arg(date).arg(durationMs / 60000);
}

QString meetingTitle(const MeetingRecord &m) {
  return withDuration(QDateTime::fromMSecsSinceEpoch(m.startMs).toString(QStringLiteral("dd.MM.yyyy HH:mm")), m.durationMs);
}

QString counters(const MeetingRecord &m) {
  const auto notes = QCoreApplication::translate("MeetingHistory", "%n note(s)", nullptr, m.noteCount);
  if (!m.summary.isEmpty())
    return m.noteCount > 0 ? QCoreApplication::translate("MeetingHistory", "AI summary · %1").arg(notes)
                           : QCoreApplication::translate("MeetingHistory", "AI summary");
  return m.noteCount > 0 ? notes : QString{};
}

QLabel *plain(const QString &text, const char *name, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setObjectName(QString::fromLatin1(name));
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

void clearLayout(QLayout *layout) {
  while (auto *item = layout->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->hide();
      widget->setParent(nullptr);
      widget->deleteLater();
    }
    delete item;
  }
}
}  // namespace

PreviousMeetingWidget::PreviousMeetingWidget(QWidget *parent)
    : NotesWidgetFrame(tr("Last time"), parent) {
  hide();
  connect(addAction(tr("Open"), "previousMeetingOpen"), &QPushButton::clicked, this,
          [this] { emit openRequested(property("materialId").toLongLong()); });
}

void PreviousMeetingWidget::setMeeting(const std::optional<MeetingRecord> &meeting) {
  clearLayout(body());
  setVisible(meeting.has_value());
  if (!meeting) return;
  setProperty("materialId", meeting->materialId);
  body()->addWidget(plain(meetingTitle(*meeting), "previousMeetingDate", this));
  body()->addWidget(plain(meetingPreview(*meeting), "previousMeetingPreview", this));
  for (const auto &agreement : meeting->agreements.mid(0, 3))
    body()->addWidget(plain(QStringLiteral("• ") + agreement, "previousMeetingAgreement", this));
}

MeetingHistoryList::MeetingHistoryList(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
}

void MeetingHistoryList::setControllerFactory(MeetingControllerFactory factory) { mFactory = std::move(factory); }

void MeetingHistoryList::setMeetings(const std::vector<MeetingRecord> &meetings, const QStringList &loose) {
  auto *column = qobject_cast<QVBoxLayout *>(layout());
  clearLayout(column);
  mMeetings.clear();
  for (const auto &meeting : meetings) {
    mMeetings.insert(meeting.materialId, meeting);
    auto *card = new NotesWidgetFrame(meetingTitle(meeting), this);
    card->setStatus(counters(meeting));
    auto *toggle = card->addAction(tr("Show"), "");
    toggle->setObjectName(QStringLiteral("historyRow_%1").arg(meeting.materialId));
    toggle->setCheckable(true);
    auto *preview = plain(meetingPreview(meeting), "historyPreview", card);
    preview->setProperty("materialId", meeting.materialId);
    preview->setObjectName(QStringLiteral("historyPreview_%1").arg(meeting.materialId));
    card->body()->addWidget(preview);
    auto *details = new QWidget(card);
    details->setObjectName(QStringLiteral("historyBody_%1").arg(meeting.materialId));
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    details->hide();
    card->body()->addWidget(details);
    connect(toggle, &QPushButton::toggled, this, [this, id = meeting.materialId](bool on) {
      if (on) {
        expand(id);
      } else if (auto *d = findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(id))) {
        d->hide();
        if (auto *p = findChild<QWidget *>(QStringLiteral("historyPreview_%1").arg(id))) p->show();
        if (auto *t = findChild<QPushButton *>(QStringLiteral("historyRow_%1").arg(id))) t->setText(tr("Show"));
      }
    });
    column->addWidget(card);
  }
  if (!loose.isEmpty()) {
    column->addWidget(plain(tr("Without a meeting"), "historyLooseTitle", this));
    for (const auto &note : loose) column->addWidget(plain(note, "historyLooseNote", this));
  }
}

void MeetingHistoryList::expand(qint64 materialId) {
  for (auto *details : findChildren<QWidget *>(QRegularExpression(QStringLiteral("^historyBody_")))) {
    const bool target = details->objectName() == QStringLiteral("historyBody_%1").arg(materialId);
    if (target && details->layout()->count() == 0) populate(details, mMeetings.value(materialId));
    details->setVisible(target);
    const auto id = details->objectName().mid(12);
    if (auto *toggle = findChild<QPushButton *>(QStringLiteral("historyRow_") + id)) {
      const QSignalBlocker blocker(toggle);
      toggle->setChecked(target);
      toggle->setText(target ? tr("Hide") : tr("Show"));
    }
    if (auto *preview = findChild<QWidget *>(QStringLiteral("historyPreview_") + id)) preview->setVisible(!target);
  }
}

void MeetingHistoryList::populate(QWidget *details, const MeetingRecord &meeting) {
  auto *layout = details->layout();
  auto *controller = mFactory ? mFactory(meeting.materialId, details) : nullptr;
  if (controller) {
    auto *summary = new AiSummaryWidget(controller, details);
    auto *points = new KeyPointsWidget(controller, details);
    connect(summary, &AiSummaryWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    connect(points, &KeyPointsWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    layout->addWidget(summary);
    layout->addWidget(points);
    points->setVisible(points->hasContent());
    connect(points, &KeyPointsWidget::contentChanged, points, [points] { points->setVisible(points->hasContent()); });
    const auto entries = controller->noteEntries();
    for (const auto &entry : entries) {
      const auto text = entry.toObject().value(QStringLiteral("body_markdown")).toString();
      if (!text.isEmpty()) layout->addWidget(plain(text, "historyNote", details));
    }
  }
  QJsonArray records;
  for (const auto &text : meeting.agreements) records.append(QJsonObject{{"text", text}, {"active", true}});
  auto *agreements = new AgreementsWidget(details);
  agreements->setRecords(records);
  agreements->setVisible(agreements->hasContent());
  layout->addWidget(agreements);
}
