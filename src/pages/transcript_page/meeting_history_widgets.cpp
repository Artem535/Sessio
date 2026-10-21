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
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString meetingTitle(const MeetingRecord &m) {
  const auto date = QDateTime::fromMSecsSinceEpoch(m.startMs).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
  if (m.durationMs <= 0) return date;
  return QCoreApplication::translate("MeetingHistory", "%1 · %2 min").arg(date).arg(m.durationMs / 60000);
}

QString rowTitle(const MeetingRecord &m) {
  const auto date = QDateTime::fromMSecsSinceEpoch(m.startMs).toString(QStringLiteral("dd.MM"));
  if (m.durationMs <= 0) return date;
  return QCoreApplication::translate("MeetingHistory", "%1 · %2 min").arg(date).arg(m.durationMs / 60000);
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
    auto *card = new QWidget(this);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);
    auto *toggle = new QToolButton(card);
    toggle->setObjectName(QStringLiteral("historyRow_%1").arg(meeting.materialId));
    toggle->setCheckable(true);
    toggle->setText(rowTitle(meeting));
    toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cardLayout->addWidget(toggle);
    cardLayout->addWidget(plain(meetingPreview(meeting), "historyPreview", card));
    auto *details = new QWidget(card);
    details->setObjectName(QStringLiteral("historyBody_%1").arg(meeting.materialId));
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    details->hide();
    cardLayout->addWidget(details);
    connect(toggle, &QToolButton::toggled, this, [this, id = meeting.materialId](bool on) {
      if (on) {
        expand(id);
      } else if (auto *d = findChild<QWidget *>(QStringLiteral("historyBody_%1").arg(id))) {
        d->hide();
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
    if (auto *toggle = findChild<QToolButton *>(QStringLiteral("historyRow_") + details->objectName().mid(12))) {
      const QSignalBlocker blocker(toggle);
      toggle->setChecked(target);
    }
  }
}

void MeetingHistoryList::populate(QWidget *details, const MeetingRecord &meeting) {
  auto *layout = details->layout();
  if (mFactory) {
    auto *controller = mFactory(meeting.materialId, details);
    auto *summary = new AiSummaryWidget(controller, details);
    auto *points = new KeyPointsWidget(controller, details);
    connect(summary, &AiSummaryWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    connect(points, &KeyPointsWidget::sourceRequested, this, &MeetingHistoryList::sourceRequested);
    layout->addWidget(summary);
    layout->addWidget(points);
    points->setVisible(points->hasContent());
    connect(points, &KeyPointsWidget::contentChanged, points, [points] { points->setVisible(points->hasContent()); });
    if (controller) {
      const auto entries = controller->noteEntries();
      for (const auto &entry : entries)
        layout->addWidget(plain(entry.toObject().value(QStringLiteral("body_markdown")).toString(), "historyNote", details));
    }
  }
  QJsonArray records;
  for (const auto &text : meeting.agreements) records.append(QJsonObject{{"text", text}, {"active", true}});
  auto *agreements = new AgreementsWidget(details);
  agreements->setRecords(records);
  agreements->setVisible(agreements->hasContent());
  layout->addWidget(agreements);
}
