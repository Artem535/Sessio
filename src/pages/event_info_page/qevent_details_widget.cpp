#include "qevent_details_widget.h"
#include "../../widgets/app_settings.h"
#include "../../widgets/meeting_utils.h"
#include "../../widgets/sensitive_clipboard.h"
#include "series_call_status_text.h"
#include "ui/pages/ui_eventdetails.h"

#include <oclero/qlementine/widgets/Switch.hpp>
#include <oclero/qlementine/widgets/LineEdit.hpp>
#include <oclero/qlementine/widgets/SegmentedControl.hpp>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QTimeZone>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(logEventDetails, "pcm.EventDetails")

namespace {
constexpr int64_t kPaymentPendingId = 1;
constexpr int64_t kPaymentPaidId = 2;
constexpr int64_t kPaymentCanceledId = 3;
constexpr int64_t kPaymentRefundedId = 4;
constexpr int64_t kPaymentSkippedId = 5;

QString weekdayToken(const int dayOfWeek) {
  switch (dayOfWeek) {
  case 1:
    return QStringLiteral("MO");
  case 2:
    return QStringLiteral("TU");
  case 3:
    return QStringLiteral("WE");
  case 4:
    return QStringLiteral("TH");
  case 5:
    return QStringLiteral("FR");
  case 6:
    return QStringLiteral("SA");
  case 7:
  default:
    return QStringLiteral("SU");
  }
}

QString weekdayToken(const QDate &date) {
  return weekdayToken(date.dayOfWeek());
}

QString rruleDateTimeUtc(const QDate &date) {
  return QDateTime(date, QTime(23, 59, 59), QTimeZone::systemTimeZone())
      .toUTC()
      .toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

QString recurrenceFrequency(const QString &type) {
  if (type == QLatin1String("daily")) {
    return QStringLiteral("DAILY");
  }
  if (type == QLatin1String("weekly")) {
    return QStringLiteral("WEEKLY");
  }
  if (type == QLatin1String("monthly")) {
    return QStringLiteral("MONTHLY");
  }
  if (type == QLatin1String("yearly")) {
    return QStringLiteral("YEARLY");
  }
  return {};
}

QString intervalSuffix(const QString &type) {
  if (type == QLatin1String("daily")) {
    return QEventDetailsWidget::tr(" day(s)");
  }
  if (type == QLatin1String("weekly")) {
    return QEventDetailsWidget::tr(" week(s)");
  }
  if (type == QLatin1String("monthly")) {
    return QEventDetailsWidget::tr(" month(s)");
  }
  if (type == QLatin1String("yearly")) {
    return QEventDetailsWidget::tr(" year(s)");
  }
  return {};
}

QString recurrenceTypeFromFrequency(const QString &frequency) {
  if (frequency == QLatin1String("DAILY")) {
    return QStringLiteral("daily");
  }
  if (frequency == QLatin1String("WEEKLY")) {
    return QStringLiteral("weekly");
  }
  if (frequency == QLatin1String("MONTHLY")) {
    return QStringLiteral("monthly");
  }
  if (frequency == QLatin1String("YEARLY")) {
    return QStringLiteral("yearly");
  }
  return QStringLiteral("none");
}

int dayOfWeekFromToken(const QString &token) {
  if (token == QLatin1String("MO")) {
    return 1;
  }
  if (token == QLatin1String("TU")) {
    return 2;
  }
  if (token == QLatin1String("WE")) {
    return 3;
  }
  if (token == QLatin1String("TH")) {
    return 4;
  }
  if (token == QLatin1String("FR")) {
    return 5;
  }
  if (token == QLatin1String("SA")) {
    return 6;
  }
  if (token == QLatin1String("SU")) {
    return 7;
  }
  return 0;
}

QHash<QString, QString> parseRRule(const QString &rule) {
  QHash<QString, QString> parts;
  for (const auto &part : rule.split(';', Qt::SkipEmptyParts)) {
    const auto separator = part.indexOf('=');
    if (separator <= 0) {
      continue;
    }
    parts.insert(part.left(separator).trimmed().toUpper(),
                 part.mid(separator + 1).trimmed().toUpper());
  }
  return parts;
}
}

QEventDetailsWidget::QEventDetailsWidget(QWidget *parent)
    : QWidget(parent), mUI(std::make_unique<Ui::EventDetails>()) {
  mUI->setupUi(this);
  initUi();
  initConnections();
  initDefaultStyle();
  initDefaultStates();
  initDefaultTimes();
}

QEventDetailsWidget::~QEventDetailsWidget() = default;


void QEventDetailsWidget::initUi() {
  const auto localTz = QTimeZone::systemTimeZone();
  mUI->mEventDate->setTimeZone(localTz);
  mUI->mTimeFrom->setTimeZone(localTz);
  mUI->mTimeTo->setTimeZone(localTz);
  mUI->mTimeFrom->setDisplayFormat("HH:mm");
  mUI->mTimeTo->setDisplayFormat("HH:mm");
  mUI->mCostSpinBox->setDecimals(2);
  mUI->mCostSpinBox->setMinimum(0.0);
  mUI->mCostSpinBox->setMaximum(1'000'000.0);
  mUI->mCostSpinBox->setSingleStep(100.0);
  mUI->mCostSpinBox->setSuffix(QStringLiteral(" ") + pcm::app_settings::currencySymbol());
  mUI->mPaymentStatusComboBox->addItem(tr("Pending"),
                                       QVariant::fromValue(kPaymentPendingId));
  mUI->mPaymentStatusComboBox->addItem(tr("Paid"),
                                       QVariant::fromValue(kPaymentPaidId));
  mUI->mPaymentStatusComboBox->addItem(tr("Canceled"),
                                       QVariant::fromValue(kPaymentCanceledId));
  mUI->mPaymentStatusComboBox->addItem(tr("Refunded"),
                                       QVariant::fromValue(kPaymentRefundedId));
  mUI->mPaymentStatusComboBox->addItem(tr("Skipped"),
                                       QVariant::fromValue(kPaymentSkippedId));

  mEventTypeSwitch = new oclero::qlementine::Switch(this);
  mEventTypeSwitch->setText(mUI->mEventType->text());
  mUI->formLayout->replaceWidget(mUI->mEventType, mEventTypeSwitch);
  mUI->mEventType->hide();
  mUI->mEventType->deleteLater();

  mOnlineSessionSwitch = new oclero::qlementine::Switch(this);
  mOnlineSessionSwitch->setText(tr("Online session"));
  mOnlineSessionSwitch->setObjectName(QStringLiteral("onlineSessionSwitch"));
  mProviderKindControl = new oclero::qlementine::SegmentedControl(this);
  mProviderKindControl->setObjectName(QStringLiteral("providerKindControl"));
  mProviderKindControl->addItem(tr("External link"), {}, {},
                                QStringLiteral("external_url"));
  mProviderKindControl->addItem(tr("LiveKit"), {}, {}, QStringLiteral("livekit"));
  mProviderKindControl->setItemsShouldExpand(true);
  mProviderKindControl->setCurrentIndex(0);
  mRepeatTypeControl = new oclero::qlementine::SegmentedControl(this);
  mRepeatTypeControl->addItem(tr("None"), {}, {}, QStringLiteral("none"));
  mRepeatTypeControl->addItem(tr("Day"), {}, {}, QStringLiteral("daily"));
  mRepeatTypeControl->addItem(tr("Week"), {}, {}, QStringLiteral("weekly"));
  mRepeatTypeControl->addItem(tr("Month"), {}, {}, QStringLiteral("monthly"));
  mRepeatTypeControl->addItem(tr("Year"), {}, {}, QStringLiteral("yearly"));
  mRepeatTypeControl->setItemsShouldExpand(true);
  mUI->mEventStatusComboBox->addItem(tr("Scheduled"), QVariant::fromValue(1));
  mUI->mEventStatusComboBox->addItem(tr("Confirmed"), QVariant::fromValue(4));
  mUI->mEventStatusComboBox->addItem(tr("Completed"), QVariant::fromValue(2));
  mUI->mEventStatusComboBox->addItem(tr("Canceled"), QVariant::fromValue(3));
  mUI->mEventStatusComboBox->addItem(tr("No show"), QVariant::fromValue(5));
  mUI->mEventStatusComboBox->addItem(tr("Rescheduled"), QVariant::fromValue(6));
  mUI->mCanceledByComboBox->addItem(tr("Not specified"), QString{});
  mUI->mCanceledByComboBox->addItem(tr("Client"), QStringLiteral("client"));
  mUI->mCanceledByComboBox->addItem(tr("Specialist"), QStringLiteral("specialist"));
  mUI->mEventStatusLabel->setVisible(false);
  mUI->mEventStatusComboBox->setVisible(false);
  mUI->mCancellationReasonLabel->setVisible(false);
  mUI->mCancellationReasonEdit->setVisible(false);
  mUI->mCanceledByLabel->setVisible(false);
  mUI->mCanceledByComboBox->setVisible(false);
  constexpr int onlineSectionRow = 10;
  mUI->formLayout->insertRow(onlineSectionRow, tr("Repeat"), mRepeatTypeControl);
  mRecurringOptionsWidget = new QWidget(this);
  auto *recurringOptionsLayout = new QHBoxLayout(mRecurringOptionsWidget);
  recurringOptionsLayout->setContentsMargins(0, 0, 0, 0);
  recurringOptionsLayout->setSpacing(6);
  recurringOptionsLayout->addWidget(new QLabel(tr("Every"), mRecurringOptionsWidget));
  mRepeatIntervalSpinBox = new QSpinBox(mRecurringOptionsWidget);
  mRepeatIntervalSpinBox->setRange(1, 52);
  mRepeatIntervalSpinBox->setValue(1);
  mRepeatIntervalSpinBox->setSuffix(tr(" week(s)"));
  mRepeatIntervalSpinBox->setMinimumWidth(112);
  recurringOptionsLayout->addWidget(mRepeatIntervalSpinBox);
  mRepeatUntilSwitch = new oclero::qlementine::Switch(mRecurringOptionsWidget);
  mRepeatUntilSwitch->setText(tr("Until"));
  mRepeatUntilSwitch->setMinimumWidth(88);
  recurringOptionsLayout->addWidget(mRepeatUntilSwitch);
  mRepeatUntilDateEdit = new QDateEdit(mRecurringOptionsWidget);
  mRepeatUntilDateEdit->setCalendarPopup(true);
  mRepeatUntilDateEdit->setDate(QDate::currentDate().addMonths(3));
  mRepeatUntilDateEdit->setEnabled(false);
  mRepeatUntilDateEdit->setMinimumWidth(122);
  recurringOptionsLayout->addWidget(mRepeatUntilDateEdit);
  recurringOptionsLayout->addStretch();
  mUI->formLayout->insertRow(onlineSectionRow + 1, QString(), mRecurringOptionsWidget);
  mWeekdayOptionsWidget = new QWidget(this);
  auto *weekdayLayout = new QHBoxLayout(mWeekdayOptionsWidget);
  weekdayLayout->setContentsMargins(0, 0, 0, 0);
  weekdayLayout->setSpacing(6);
  const QVector<QPair<QString, int>> weekdays = {
      {tr("Mon"), 1}, {tr("Tue"), 2}, {tr("Wed"), 3}, {tr("Thu"), 4},
      {tr("Fri"), 5}, {tr("Sat"), 6}, {tr("Sun"), 7}};
  for (const auto &[label, day] : weekdays) {
    auto *button = new QPushButton(mWeekdayOptionsWidget);
    button->setText(label);
    button->setCheckable(true);
    button->setProperty("dayOfWeek", day);
    button->setMinimumSize(52, 30);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        " color: #ffffff;"
        " background-color: #363b46;"
        " border: 1px solid #566070;"
        " border-radius: 6px;"
        " font-weight: 600;"
        " padding: 0;"
        "}"
        "QPushButton:hover {"
        " background-color: #404756;"
        "}"
        "QPushButton:checked {"
        " color: #ffffff;"
        " background-color: #4f83ff;"
        " border-color: #4f83ff;"
        "}"));
    weekdayLayout->addWidget(button);
    mWeekdayButtons.append(button);
    connect(button, &QPushButton::toggled, this, [this]() { updateButtonState(); });
  }
  weekdayLayout->addStretch();
  mUI->formLayout->insertRow(onlineSectionRow + 2, tr("Days"),
                             mWeekdayOptionsWidget);
  mUI->formLayout->insertRow(onlineSectionRow + 3, tr("Session format"),
                             mOnlineSessionSwitch);
  mUI->formLayout->insertRow(onlineSectionRow + 4, tr("Provider"),
                             mProviderKindControl);

  mMeetingUrlLabel = new QLabel(tr("Meeting link"), this);
  mMeetingUrlEdit = new oclero::qlementine::LineEdit(this);
  mMeetingUrlEdit->setObjectName(QStringLiteral("meetingUrlEdit"));
  mMeetingUrlEdit->setPlaceholderText(tr("https://..."));
  mMeetingUrlEdit->setIcon(QIcon(":/icons/calendar-solid-full.svg"));
  mUI->formLayout->insertRow(onlineSectionRow + 5, mMeetingUrlLabel,
                             mMeetingUrlEdit);

  mMeetingActionsWidget = new QWidget(this);
  auto *meetingActionsLayout = new QHBoxLayout(mMeetingActionsWidget);
  meetingActionsLayout->setContentsMargins(0, 0, 0, 0);
  meetingActionsLayout->setSpacing(8);
  mOpenMeetingButton = new QPushButton(tr("Open"), mMeetingActionsWidget);
  mCopyMeetingUrlButton = new QPushButton(tr("Copy link"), mMeetingActionsWidget);
  mCopyMeetingInviteButton = new QPushButton(tr("Copy invite"), mMeetingActionsWidget);
  mCopyMeetingPasscodeButton = new QPushButton(tr("Copy passcode"), mMeetingActionsWidget);
  mCopyMeetingPasscodeButton->setObjectName(QStringLiteral("copyMeetingPasscodeButton"));
  mCopyMeetingPasscodeButton->setVisible(false); // series calls only
  mOpenMeetingButton->setObjectName(QStringLiteral("openMeetingButton"));
  mCopyMeetingUrlButton->setObjectName(QStringLiteral("copyMeetingUrlButton"));
  mCopyMeetingInviteButton->setObjectName(QStringLiteral("copyMeetingInviteButton"));
  meetingActionsLayout->addWidget(mOpenMeetingButton);
  meetingActionsLayout->addWidget(mCopyMeetingUrlButton);
  meetingActionsLayout->addWidget(mCopyMeetingInviteButton);
  meetingActionsLayout->addWidget(mCopyMeetingPasscodeButton);
  meetingActionsLayout->addStretch();
  mUI->formLayout->insertRow(onlineSectionRow + 6, QString(),
                             mMeetingActionsWidget);

  mSeriesCallWidget = new QWidget(this);
  mSeriesCallWidget->setObjectName(QStringLiteral("seriesCallPanel"));
  auto *seriesCallLayout = new QVBoxLayout(mSeriesCallWidget);
  seriesCallLayout->setContentsMargins(0, 0, 0, 0);
  seriesCallLayout->setSpacing(6);
  mSeriesStatusLabel = new QLabel(mSeriesCallWidget);
  mSeriesStatusLabel->setObjectName(QStringLiteral("seriesCallStatusLabel"));
  mSeriesStatusLabel->setWordWrap(true);
  mSeriesStatusLabel->setTextFormat(Qt::PlainText);
  seriesCallLayout->addWidget(mSeriesStatusLabel);
  auto *seriesActionsLayout = new QHBoxLayout();
  seriesActionsLayout->setContentsMargins(0, 0, 0, 0);
  seriesActionsLayout->setSpacing(8);
  mSeriesRetryButton = new QPushButton(tr("Retry"), mSeriesCallWidget);
  mSeriesRetryButton->setObjectName(QStringLiteral("seriesCallRetryButton"));
  mSeriesReissueButton = new QPushButton(tr("Create new link"), mSeriesCallWidget);
  mSeriesReissueButton->setObjectName(QStringLiteral("seriesCallReissueButton"));
  mSeriesPublishButton = new QPushButton(tr("Publish this device's schedule"), mSeriesCallWidget);
  mSeriesPublishButton->setObjectName(QStringLiteral("seriesCallPublishButton"));
  mMigrateButton = new QPushButton(tr("Move to a permanent link..."), mSeriesCallWidget);
  mMigrateButton->setObjectName(QStringLiteral("migrateToPermanentLinkButton"));
  seriesActionsLayout->addWidget(mSeriesRetryButton);
  seriesActionsLayout->addWidget(mSeriesReissueButton);
  seriesActionsLayout->addWidget(mSeriesPublishButton);
  seriesActionsLayout->addWidget(mMigrateButton);
  seriesActionsLayout->addStretch();
  seriesCallLayout->addLayout(seriesActionsLayout);
  mSeriesCallWidget->setVisible(false);
  mUI->formLayout->insertRow(onlineSectionRow + 7, QString(), mSeriesCallWidget);

  mBuffersWidget = new QWidget(this);
  auto *buffersLayout = new QHBoxLayout(mBuffersWidget);
  buffersLayout->setContentsMargins(0, 0, 0, 0);
  buffersLayout->setSpacing(8);
  buffersLayout->addWidget(new QLabel(tr("Before"), mBuffersWidget));
  mBufferBeforeSpinBox = new QSpinBox(mBuffersWidget);
  mBufferBeforeSpinBox->setRange(0, 240);
  mBufferBeforeSpinBox->setSuffix(tr(" min"));
  mBufferBeforeSpinBox->setMinimumWidth(92);
  buffersLayout->addWidget(mBufferBeforeSpinBox);
  buffersLayout->addWidget(new QLabel(tr("After"), mBuffersWidget));
  mBufferAfterSpinBox = new QSpinBox(mBuffersWidget);
  mBufferAfterSpinBox->setRange(0, 240);
  mBufferAfterSpinBox->setSuffix(tr(" min"));
  mBufferAfterSpinBox->setMinimumWidth(92);
  buffersLayout->addWidget(mBufferAfterSpinBox);
  buffersLayout->addStretch();
  mUI->formLayout->insertRow(onlineSectionRow + 8, tr("Buffers"),
                             mBuffersWidget);

  auto *conflictWidget = new QWidget(this);
  auto *conflictLayout = new QVBoxLayout(conflictWidget);
  conflictLayout->setContentsMargins(0, 0, 0, 0);
  conflictLayout->setSpacing(6);
  mConflictWarningLabel = new QLabel(conflictWidget);
  mConflictWarningLabel->setWordWrap(true);
  mConflictWarningLabel->setStyleSheet("color: #f0c36d;");
  mConflictWarningLabel->setVisible(false);
  mSuggestFreeSlotButton = new QPushButton(tr("Suggest free slot"), conflictWidget);
  mSuggestFreeSlotButton->setVisible(false);
  conflictLayout->addWidget(mConflictWarningLabel);
  conflictLayout->addWidget(mSuggestFreeSlotButton, 0, Qt::AlignLeft);
  mUI->formLayout->insertRow(onlineSectionRow + 9, QString(), conflictWidget);

  mUI->mAddButton->setIcon(QIcon(":/icons/calendar-plus-solid-full.svg"));
  mUI->mAddButton->setIconSize(QSize(16, 16));
  mUI->mChangeButton->setIcon(QIcon(":/icons/user-pen-solid-full.svg"));
  mUI->mChangeButton->setIconSize(QSize(16, 16));
}

void QEventDetailsWidget::initConnections() {
  // --- Button Connections ---
  connect(mUI->mButtonBox->button(QDialogButtonBox::Apply),
          &QPushButton::clicked, this, &QEventDetailsWidget::onApplyClicked);
  connect(mUI->mButtonBox->button(QDialogButtonBox::Cancel),
          &QPushButton::clicked, this, &QEventDetailsWidget::onCancelClicked);
  connect(mUI->mAddButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onAddClicked);
  connect(mUI->mChangeButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onChangeClicked);

  // --- Input Change Connections ---
  connect(mEventTypeSwitch, &QAbstractButton::toggled, this,
          &QEventDetailsWidget::onEventTypeToggled);
  connect(mOnlineSessionSwitch, &QAbstractButton::toggled, this,
          &QEventDetailsWidget::onOnlineSessionToggled);
  connect(mRepeatTypeControl, &oclero::qlementine::SegmentedControl::currentIndexChanged, this,
          &QEventDetailsWidget::onRecurrenceTypeChanged);
  connect(mRepeatUntilSwitch, &QAbstractButton::toggled, mRepeatUntilDateEdit,
          &QWidget::setEnabled);
  connect(mUI->mEventStatusComboBox,
          qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](const int) { updateCancellationControls(); });
  connect(mMeetingUrlEdit, &QLineEdit::textChanged, this,
          &QEventDetailsWidget::onMeetingUrlChanged);
  connect(mOpenMeetingButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onOpenMeetingClicked);
  connect(mCopyMeetingUrlButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onCopyMeetingUrlClicked);
  connect(mCopyMeetingInviteButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onCopyMeetingInviteClicked);
  connect(mCopyMeetingPasscodeButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onCopyMeetingPasscodeClicked);
  connect(mSeriesRetryButton, &QPushButton::clicked, this, [this]() {
    if (mSeriesCalls) {
      mSeriesCalls->retry(mSeriesId);
    }
  });
  connect(mSeriesReissueButton, &QPushButton::clicked, this, [this]() {
    if (!mSeriesCalls) {
      return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Create a new link"),
        tr("The current link stops working immediately. Anyone who has it will need the new "
           "link. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
      mSeriesCalls->reissueInvitation(mSeriesId);
    }
  });
  connect(mSeriesPublishButton, &QPushButton::clicked, this, [this]() {
    if (!mSeriesCalls) {
      return;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Publish this device's schedule"),
        tr("The server holds a different version of this schedule. Publishing replaces it with "
           "the schedule on this device. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
      mSeriesCalls->publishThisDeviceSchedule(mSeriesId);
    }
  });
  connect(mMigrateButton, &QPushButton::clicked, this,
          [this]() { emit migrateToPermanentLinkRequested(mSeriesId); });
  connect(mUI->mTimeFrom, &QTimeEdit::timeChanged, this,
          &QEventDetailsWidget::onTimeFromChanged);
  connect(mUI->mTimeTo, &QTimeEdit::timeChanged, this,
          &QEventDetailsWidget::onTimeToChanged);
  connect(mUI->mEventDate, &QDateEdit::dateChanged, this, [this](const QDate &date) {
    if (mRepeatTypeControl->currentData().toString() == QLatin1String("weekly") &&
        selectedWeekdayRule().isEmpty()) {
      selectWeekday(date.dayOfWeek(), true);
    }
    updateConflictWarning();
  });
  connect(mBufferBeforeSpinBox, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) { updateConflictWarning(); });
  connect(mBufferAfterSpinBox, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) { updateConflictWarning(); });
  connect(mSuggestFreeSlotButton, &QPushButton::clicked, this,
          &QEventDetailsWidget::onSuggestFreeSlotClicked);
}

void QEventDetailsWidget::initDefaultStyle() {
  if (mDialogMode) {
    initEditStyle();
    return;
  }

  const auto isVisible = mCurrentEvent ? mCurrentEvent->isWorkItem() : false;
  mUI->mClientComboBox->setVisible(isVisible);
  mUI->mClientComboxBoxLabel->setVisible(isVisible);
  mUI->mCostLabel->setVisible(isVisible);
  mUI->mCostSpinBox->setVisible(isVisible);
  mUI->mPaymentStatusLabel->setVisible(isVisible);
  mUI->mPaymentStatusComboBox->setVisible(isVisible);
  mUI->mEventStatusLabel->setVisible(isVisible);
  mUI->mEventStatusComboBox->setVisible(isVisible);
  updateCancellationControls();
  mEventTypeSwitch->setEnabled(false);
  updateRecurringControls();
  mUI->mButtonBox->setVisible(false);
  mUI->mChangeButton->setVisible(true);
  mUI->mAddButton->setVisible(true);
  onOnlineSessionToggled(mOnlineSessionSwitch->isChecked());
  emit provideFillClientComboBox(mUI->mClientComboBox);
}

void QEventDetailsWidget::initEditStyle() {
  mUI->mButtonBox->setVisible(true);
  mUI->mChangeButton->setVisible(false);
  mUI->mAddButton->setVisible(false);
  mEventTypeSwitch->setEnabled(true);
  updateRecurringControls();
  onEventTypeToggled(mEventTypeSwitch->isChecked());
  onOnlineSessionToggled(mOnlineSessionSwitch->isChecked());
  mUI->mEventStatusLabel->setVisible(true);
  mUI->mEventStatusComboBox->setVisible(true);
  updateCancellationControls();
  emit provideFillClientComboBox(mUI->mClientComboBox);
}

void QEventDetailsWidget::initDefaultStates() const {
  // Set initial text for checkbox
  mEventTypeSwitch->setText(tr(": EVENT_TYPE_REGULAR"));
}

void QEventDetailsWidget::initDefaultTimes() const {
  const auto crtDateTime = QDateTime::currentDateTime();
  mUI->mEventDate->setDate(crtDateTime.date());
  mUI->mTimeFrom->setTime(crtDateTime.time());
  mUI->mTimeTo->setTime(crtDateTime.addSecs(3600).time()); // +1 hour
}

void QEventDetailsWidget::setClientList(const QHash<int64_t, QString> &clients) {
  mClientList = clients;
  mUI->mClientComboBox->clear();
  for (auto it = mClientList.constBegin(); it != mClientList.constEnd(); ++it) {
    mUI->mClientComboBox->addItem(it.value(), QVariant::fromValue(it.key()));
  }
}

void QEventDetailsWidget::loadEvent(QEventItem *event,
                                    const std::optional<int64_t> clientId) {
  if (!event)
    return;

  qCDebug(logEventDetails) << "EventDetailsWidget::loadEvent|"
                           << "Start time:" << event->getStartTime()
                           << "End time:" << event->getEndTime()
                           << "ID:" << event->getId();

  mCurrentEvent = event;
  // A newly loaded event starts with no apply in flight; its stored schedule
  // is what its LiveKit meeting (if any) was created for.
  mPendingMeetingCreation = false;
  mSupersededLiveKitMeetingRef.clear();
  mMeetingScheduledStart = event->getStartTime();
  mMeetingScheduledEnd = event->getEndTime();
  mUI->mTitle->setText(event->getTitle());
  mUI->mEventDate->setDate(event->getStartTime().date());
  mUI->mTimeFrom->setTime(event->getStartTime().time());
  mUI->mTimeTo->setTime(event->getEndTime().time());
  const bool isWorkItem = event->isWorkItem();
  mEventTypeSwitch->setChecked(isWorkItem);
  mOnlineSessionSwitch->setChecked(event->isOnline());
  mProviderKindControl->setCurrentIndex(
      event->providerKind() == pcm::meeting::ProviderKind::LiveKit ? 1 : 0);
  mMeetingUrlEdit->setText(event->meetingUrl());
  mUI->mCostSpinBox->setValue(
      event->cost().value_or(pcm::app_settings::defaultWorkEventCost()));
  const auto paymentStatusData = QVariant::fromValue(event->paymentStatusId());
  if (const int paymentIndex =
          mUI->mPaymentStatusComboBox->findData(paymentStatusData);
      paymentIndex != -1) {
    mUI->mPaymentStatusComboBox->setCurrentIndex(paymentIndex);
  }
  const auto eventStatusData = QVariant::fromValue(event->eventStatusId());
  if (const int eventStatusIndex =
          mUI->mEventStatusComboBox->findData(eventStatusData);
      eventStatusIndex != -1) {
    mUI->mEventStatusComboBox->setCurrentIndex(eventStatusIndex);
  }
  mUI->mCancellationReasonEdit->setText(event->cancellationReason());
  const auto canceledByIndex =
      mUI->mCanceledByComboBox->findData(event->canceledBy());
  mUI->mCanceledByComboBox->setCurrentIndex(canceledByIndex >= 0 ? canceledByIndex : 0);
  mBufferBeforeSpinBox->setValue(
      static_cast<int>(event->bufferBeforeMinutes()));
  mBufferAfterSpinBox->setValue(
      static_cast<int>(event->bufferAfterMinutes()));

  if (isWorkItem && event->getId() != 0) {
    // Find selected client ID in the client list
    if (clientId) {
      // TODO: Find a way to avoid this
      emit provideFillClientComboBox(mUI->mClientComboBox);
      const auto varClientId = QVariant::fromValue(clientId.value());
      if (const int index = mUI->mClientComboBox->findData(varClientId);
          index != -1) {
        mUI->mClientComboBox->setCurrentIndex(index);
      }
    }
  }

  updateButtonState();
  updateConflictWarning();
  onEventTypeToggled(isWorkItem);
  onOnlineSessionToggled(event->isOnline());
  updateCancellationControls();
}

void QEventDetailsWidget::startEditingEvent(
    QEventItem *event, const std::optional<int64_t> clientId) {
  if (!event) {
    return;
  }

  mCreatingNewEvent = false;
  mInEditMode = true;
  emit provideEditModeChanged();

  loadEvent(event, clientId);
  initEditStyle();
}

void QEventDetailsWidget::startCreatingNewEvent(const QDate &date,
                                                const std::optional<QTime> startTime,
                                                const std::optional<int> durationMinutes) {
  mCreatingNewEvent = true;
  mInEditMode = true;
  emit provideEditModeChanged();

  const auto crtDateTime = QDateTime::currentDateTime().toLocalTime();
  // Create a temporary event for the form
  mCurrentEvent =
      new QEventItem(0, tr(": EVENT_NEW_TITLE"), crtDateTime,
                     crtDateTime.addSecs(3600));
  loadEvent(mCurrentEvent.data());
  mUI->mEventDate->setDate(date);
  mRepeatUntilDateEdit->setMinimumDate(date);
  mRepeatUntilDateEdit->setDate(date.addMonths(3));
  selectWeekday(date.dayOfWeek(), true);
  if (startTime.has_value()) {
    const auto duration = durationMinutes.value_or(
        pcm::app_settings::defaultSessionDurationMinutes());
    mUI->mTimeFrom->setTime(*startTime);
    mUI->mTimeTo->setTime(startTime->addSecs(duration * 60));
  }
  mUI->mCostSpinBox->setValue(pcm::app_settings::defaultWorkEventCost());
  mUI->mPaymentStatusComboBox->setCurrentIndex(
      mUI->mPaymentStatusComboBox->findData(QVariant::fromValue(kPaymentPendingId)));
  mUI->mEventStatusComboBox->setCurrentIndex(
      mUI->mEventStatusComboBox->findData(QVariant::fromValue(1)));
  mUI->mCancellationReasonEdit->clear();
  mUI->mCanceledByComboBox->setCurrentIndex(0);
  mBufferBeforeSpinBox->setValue(pcm::app_settings::defaultBufferBeforeMinutes());
  mBufferAfterSpinBox->setValue(pcm::app_settings::defaultBufferAfterMinutes());
  updateCancellationControls();
  initEditStyle();
}

void QEventDetailsWidget::setDialogMode(bool enabled) {
  mDialogMode = enabled;
  initDefaultStyle();
}

bool QEventDetailsWidget::isInEditMode() const { return mInEditMode; }

bool QEventDetailsWidget::isCreatingNewEvent() const {
  return mCreatingNewEvent;
}

int64_t QEventDetailsWidget::selectedClientId() const {
  if (!mEventTypeSwitch->isChecked()) {
    return 0;
  }

  return mUI->mClientComboBox->currentData().toLongLong();
}

QString QEventDetailsWidget::selectedClientName() const {
  if (!mEventTypeSwitch->isChecked()) {
    return {};
  }

  return mUI->mClientComboBox->currentText();
}

bool QEventDetailsWidget::wantsNewLiveKitSeries() const {
  return mCreatingNewEvent && isRecurring() && mOnlineSessionSwitch->isChecked() &&
         mProviderKindControl->currentIndex() == 1 && !mLegacyMigratable;
}

bool QEventDetailsWidget::isRecurring() const {
  return mRepeatTypeControl &&
         mRepeatTypeControl->currentData().toString() != QLatin1String("none");
}

QString QEventDetailsWidget::recurrenceRule() const {
  if (!isRecurring()) {
    return {};
  }

  const auto date = mUI->mEventDate->date();
  const auto frequency = recurrenceFrequency(mRepeatTypeControl->currentData().toString());
  auto rule = QStringLiteral("FREQ=%1;INTERVAL=%2")
                  .arg(frequency)
                  .arg(mRepeatIntervalSpinBox ? mRepeatIntervalSpinBox->value() : 1);
  if (frequency == QLatin1String("WEEKLY")) {
    const auto weekdays = selectedWeekdayRule();
    rule += QStringLiteral(";BYDAY=%1").arg(weekdays.isEmpty() ? weekdayToken(date) : weekdays);
  } else if (frequency == QLatin1String("MONTHLY")) {
    rule += QStringLiteral(";BYMONTHDAY=%1").arg(date.day());
  } else if (frequency == QLatin1String("YEARLY")) {
    rule += QStringLiteral(";BYMONTH=%1;BYMONTHDAY=%2").arg(date.month()).arg(date.day());
  }
  if (mRepeatUntilSwitch && mRepeatUntilSwitch->isChecked()) {
    rule += QStringLiteral(";UNTIL=%1").arg(rruleDateTimeUtc(mRepeatUntilDateEdit->date()));
  }
  return rule;
}

std::optional<int64_t> QEventDetailsWidget::recurrenceUntilMs() const {
  if (!isRecurring() || !mRepeatUntilSwitch || !mRepeatUntilSwitch->isChecked()) {
    return std::nullopt;
  }

  return QDateTime(mRepeatUntilDateEdit->date(), QTime(23, 59, 59),
                   QTimeZone::systemTimeZone())
      .toUTC()
      .toMSecsSinceEpoch();
}

void QEventDetailsWidget::setRecurrenceRule(
    const QString &rule, const std::optional<int64_t> recurrenceUntilMs) {
  const auto parts = parseRRule(rule);
  const auto type = recurrenceTypeFromFrequency(parts.value(QStringLiteral("FREQ")));
  mRepeatTypeControl->setCurrentData(type);
  mRepeatIntervalSpinBox->setValue(
      std::max(1, parts.value(QStringLiteral("INTERVAL"), QStringLiteral("1")).toInt()));

  for (auto *button : mWeekdayButtons) {
    if (button) {
      button->setChecked(false);
    }
  }

  if (type == QLatin1String("weekly")) {
    const auto dayTokens = parts.value(QStringLiteral("BYDAY")).split(',', Qt::SkipEmptyParts);
    for (const auto &token : dayTokens) {
      const auto day = dayOfWeekFromToken(token.trimmed());
      for (auto *button : mWeekdayButtons) {
        if (button && button->property("dayOfWeek").toInt() == day) {
          button->setChecked(true);
        }
      }
    }
    if (selectedWeekdayRule().isEmpty()) {
      selectWeekday(mUI->mEventDate->date().dayOfWeek(), true);
    }
  }

  if (recurrenceUntilMs.has_value()) {
    const auto until = QDateTime::fromMSecsSinceEpoch(*recurrenceUntilMs,
                                                      QTimeZone::UTC)
                           .toLocalTime()
                           .date();
    mRepeatUntilSwitch->setChecked(true);
    mRepeatUntilDateEdit->setDate(until);
  } else {
    mRepeatUntilSwitch->setChecked(false);
  }

  onRecurrenceTypeChanged();
  updateRecurringControls();
}

void QEventDetailsWidget::rejectPendingSave() {
  mSaveAccepted = false;
}

void QEventDetailsWidget::setConflictChecker(
    std::function<std::optional<DuckEvent>(const DuckEvent &)> checker) {
  mConflictChecker = std::move(checker);
  updateConflictWarning();
}

void QEventDetailsWidget::setMeetingCoordinator(pcm::meeting::MeetingCoordinator *coordinator) {
  if (mMeetingCoordinator) {
    disconnect(mMeetingCoordinator, &pcm::meeting::MeetingCoordinator::meetingCreated, this,
              &QEventDetailsWidget::onMeetingCreated);
    disconnect(mMeetingCoordinator, &pcm::meeting::MeetingCoordinator::meetingCreateFailed,
               this, &QEventDetailsWidget::onMeetingCreateFailed);
  }
  mMeetingCoordinator = coordinator;
  if (mMeetingCoordinator) {
    connect(mMeetingCoordinator, &pcm::meeting::MeetingCoordinator::meetingCreated, this,
            &QEventDetailsWidget::onMeetingCreated);
    connect(mMeetingCoordinator, &pcm::meeting::MeetingCoordinator::meetingCreateFailed, this,
            &QEventDetailsWidget::onMeetingCreateFailed);
  }
}

void QEventDetailsWidget::onMeetingCreated(const pcm::meeting::MeetingDescriptor descriptor) {
  if (!mPendingMeetingCreation && !mAwaitingSyncMeetingResult) {
    // The coordinator is shared: this result belongs to a request this widget
    // did not make (or to an apply that was canceled meanwhile).
    qCDebug(logEventDetails) << "Ignoring a meeting-created result with no apply in flight";
    return;
  }
  if (!mCurrentEvent) {
    mPendingMeetingCreation = false;
    updateButtonState();
    return;
  }
  const auto meetingUrl = descriptor.meetingUrl.value_or(QString{});
  applyProviderFields(descriptor.kind, descriptor.meetingRef, descriptor.invitationState, meetingUrl);
  mMeetingUrlEdit->setText(meetingUrl);

  if (!mPendingMeetingCreation) {
    // Synchronous ExternalUrl result: onApplyClicked continues the apply.
    updateButtonState();
    return;
  }

  // The deferred half of onApplyClicked: the LiveKit meeting now exists, so
  // the event can be saved with its real meetingRef. mCreatingNewEvent is
  // still the value it had when Apply was clicked — only finishApply resets
  // it.
  mPendingMeetingCreation = false;
  mMeetingScheduledStart = mCurrentEvent->getStartTime();
  mMeetingScheduledEnd = mCurrentEvent->getEndTime();
  updateButtonState();
  finishApply(mCreatingNewEvent);
}

void QEventDetailsWidget::onMeetingCreateFailed(const QString &error) {
  if (!mPendingMeetingCreation) {
    qCWarning(logEventDetails) << "Meeting create failed with no apply in flight:" << error;
    return;
  }

  // The event is not saved: the user stays in the form and can retry Apply or
  // switch the online session/provider off. Any LiveKit meeting this apply
  // meant to replace is still the stored one, so it is left untouched.
  mPendingMeetingCreation = false;
  updateButtonState();
  qCWarning(logEventDetails) << "LiveKit meeting create failed:" << error;
  QMessageBox::warning(this, tr(": ERROR_TITLE"),
                       tr("Failed to create the LiveKit meeting: %1").arg(error));
}

void QEventDetailsWidget::applyProviderFields(
    const std::optional<pcm::meeting::ProviderKind> kind, const QString &meetingRef,
    const std::optional<QString> &invitationState, const QString &meetingUrl) {
  mCurrentEvent->setProviderKind(kind);
  mCurrentEvent->setMeetingRef(meetingRef);
  mCurrentEvent->setInvitationState(invitationState);
  mCurrentEvent->setMeetingUrl(meetingUrl);
}

QEventItem *QEventDetailsWidget::currentEvent() const {
  return mCurrentEvent.data();
}

void QEventDetailsWidget::onApplyClicked() {
  if (mPendingMeetingCreation) {
    // An earlier Apply is still waiting for its LiveKit meeting.
    return;
  }
  if (!validateInput()) {
    return;
  }

  if (mCurrentEvent) {
    const auto startDateTime = QDateTime(mUI->mEventDate->date(),
                                         mUI->mTimeFrom->time(),
                                         QTimeZone::systemTimeZone());
    const auto endDateTime = QDateTime(mUI->mEventDate->date(),
                                       mUI->mTimeTo->time(),
                                       QTimeZone::systemTimeZone());
    qCDebug(logEventDetails) << "Apply event from form:"
                             << "date=" << mUI->mEventDate->date()
                             << "timeFrom=" << mUI->mTimeFrom->time()
                             << "timeTo=" << mUI->mTimeTo->time()
                             << "startLocal=" << startDateTime.toString(Qt::ISODate)
                             << "startUtc=" << startDateTime.toUTC().toString(Qt::ISODate)
                             << "startMs=" << startDateTime.toMSecsSinceEpoch()
                             << "endLocal=" << endDateTime.toString(Qt::ISODate)
                             << "endUtc=" << endDateTime.toUTC().toString(Qt::ISODate)
                             << "endMs=" << endDateTime.toMSecsSinceEpoch();

    mCurrentEvent->setTitle(mUI->mTitle->text());
    mCurrentEvent->setTimeRange(startDateTime, endDateTime);
    mCurrentEvent->setIsWorkItem(mEventTypeSwitch->isChecked());
    mCurrentEvent->setCost(mEventTypeSwitch->isChecked()
                               ? std::make_optional(mUI->mCostSpinBox->value())
                               : std::nullopt);
    mCurrentEvent->setPaymentStatusId(
        mEventTypeSwitch->isChecked()
            ? mUI->mPaymentStatusComboBox->currentData().toLongLong()
            : kPaymentSkippedId);
    mCurrentEvent->setEventStatusId(
        mUI->mEventStatusComboBox->currentData().toLongLong());
    mCurrentEvent->setCancellationReason(
        mUI->mCancellationReasonEdit->text());
    mCurrentEvent->setCanceledBy(
        mUI->mCanceledByComboBox->currentData().toString());
    mCurrentEvent->setOnline(mOnlineSessionSwitch->isChecked());
    mCurrentEvent->setBufferBeforeMinutes(mBufferBeforeSpinBox->value());
    mCurrentEvent->setBufferAfterMinutes(mBufferAfterSpinBox->value());

    // The conflict check only depends on the schedule and buffers set above,
    // so it runs before any meeting side effect: a rejected apply must not
    // create (or replace) a backend meeting.
    if (mConflictChecker) {
      if (const auto conflict = mConflictChecker(mCurrentEvent->toEvent());
          conflict.has_value()) {
        QMessageBox::warning(this, tr(": ERROR_TITLE"), conflictWarningText(*conflict));
        return;
      }
    }

    if (!updateMeetingViaCoordinator()) {
      // A LiveKit create is in flight: onMeetingCreated saves the event once
      // the meeting exists, onMeetingCreateFailed keeps the form open instead.
      // updateButtonState() disables Apply while mPendingMeetingCreation is
      // set, so the apply cannot be submitted twice.
      updateButtonState();
      return;
    }
  }

  finishApply(mCreatingNewEvent);
}

void QEventDetailsWidget::finishApply(const bool isCreatingNewEvent) {
  // Emit signal to save the event
  mSaveAccepted = true;
  emit provideEventSave(mCurrentEvent.data());
  if (!mSaveAccepted) {
    return;
  }

  if (isCreatingNewEvent && (!mCurrentEvent || mCurrentEvent->getId() <= 0)) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr("Failed to save event to database"));
    return;
  }

  // The save went through, so the meeting it replaced can go now.
  cancelSupersededLiveKitMeeting();

  if (isCreatingNewEvent && mCurrentEvent) {
    delete mCurrentEvent.data();
    mCurrentEvent.clear();
  }

  // Exit edit mode
  mInEditMode = false;
  mCreatingNewEvent = false;
  emit provideEditModeChanged();
  initDefaultStyle();
  emit provideDialogAccept();
}

void QEventDetailsWidget::onCancelClicked() {
  mInEditMode = false;
  // Nothing is saved, so a late meeting result must not finish this apply,
  // and the stored meeting stays the event's meeting.
  mPendingMeetingCreation = false;
  mSupersededLiveKitMeetingRef.clear();
  if (mCreatingNewEvent && mCurrentEvent) {
    delete mCurrentEvent.data();
    mCurrentEvent.clear();
  }
  mCreatingNewEvent = false;
  emit provideEditModeChanged();
  emit provideEditingCanceled();
  initDefaultStyle();
}

void QEventDetailsWidget::onAddClicked() { startCreatingNewEvent(); }

void QEventDetailsWidget::onChangeClicked() {
  if (!mCurrentEvent)
    return;
  mInEditMode = true;
  emit provideEditModeChanged();
  initEditStyle();
}

void QEventDetailsWidget::onEventTypeToggled(bool checked) {
  mEventTypeSwitch->setText(
      checked ? tr(": EVENT_TYPE_WORK") : tr(": EVENT_TYPE_REGULAR"));
  mUI->mClientComboBox->setVisible(checked);
  mUI->mClientComboxBoxLabel->setVisible(checked);
  mUI->mCostLabel->setVisible(checked);
  mUI->mCostSpinBox->setVisible(checked);
  mUI->mPaymentStatusLabel->setVisible(checked);
  mUI->mPaymentStatusComboBox->setVisible(checked);
  if (checked && mCreatingNewEvent && mUI->mCostSpinBox->value() <= 0.0) {
    mUI->mCostSpinBox->setValue(pcm::app_settings::defaultWorkEventCost());
  }
  if (!checked) {
    const auto skippedIndex =
        mUI->mPaymentStatusComboBox->findData(QVariant::fromValue(kPaymentSkippedId));
    if (skippedIndex != -1) {
      mUI->mPaymentStatusComboBox->setCurrentIndex(skippedIndex);
    }
  } else if (mUI->mPaymentStatusComboBox->currentData().toLongLong() == kPaymentSkippedId) {
    const auto pendingIndex =
        mUI->mPaymentStatusComboBox->findData(QVariant::fromValue(kPaymentPendingId));
    if (pendingIndex != -1) {
      mUI->mPaymentStatusComboBox->setCurrentIndex(pendingIndex);
    }
  }
}

void QEventDetailsWidget::onOnlineSessionToggled(const bool checked) {
  mProviderKindControl->setVisible(checked);
  if (auto *label = mUI->formLayout->labelForField(mProviderKindControl)) {
    label->setVisible(checked);
  }
  mMeetingUrlLabel->setVisible(checked);
  mMeetingUrlEdit->setVisible(checked);
  mMeetingActionsWidget->setVisible(checked);
  updateButtonState();
}

void QEventDetailsWidget::onRecurrenceTypeChanged() {
  mRepeatIntervalSpinBox->setSuffix(
      intervalSuffix(mRepeatTypeControl->currentData().toString()));
  if (mRepeatTypeControl->currentData().toString() == QLatin1String("weekly") &&
      selectedWeekdayRule().isEmpty()) {
    selectWeekday(mUI->mEventDate->date().dayOfWeek(), true);
  }
  updateRecurringControls();
  updateButtonState();
}

void QEventDetailsWidget::onMeetingUrlChanged(const QString &url) {
  Q_UNUSED(url)
  updateButtonState();
}

void QEventDetailsWidget::onOpenMeetingClicked() {
  if (mSeriesBacked) {
    emit openLiveKitMeetingRequested(mSeriesJoinTarget);
    return;
  }
  if (mCurrentEvent &&
      mCurrentEvent->providerKind() == pcm::meeting::ProviderKind::LiveKit) {
    emit openLiveKitMeetingRequested(mCurrentEvent->meetingRef());
    return;
  }
  pcm::meeting::openMeetingUrl(mMeetingUrlEdit->text(), this);
}

void QEventDetailsWidget::onCopyMeetingUrlClicked() {
  if (mSeriesBacked) {
    withSeriesInvitation([](const QString &url, const QString &) { pcm::meeting::copyMeetingUrl(url); });
    return;
  }
  if (!pcm::meeting::isValidInvitationUrl(mMeetingUrlEdit->text())) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr("Enter a valid http or https meeting link."));
    return;
  }

  pcm::meeting::copyMeetingUrl(mMeetingUrlEdit->text());
}

void QEventDetailsWidget::onCopyMeetingInviteClicked() {
  if (mSeriesBacked) {
    const auto clientName = selectedClientName();
    const auto startMs = mCurrentEvent ? mCurrentEvent->toEvent().start_date.value_or(0) : 0;
    withSeriesInvitation([clientName, startMs](const QString &url, const QString &) {
      pcm::meeting::copyMeetingInvite(url, clientName, startMs);
    });
    return;
  }
  if (!pcm::meeting::isValidInvitationUrl(mMeetingUrlEdit->text())) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr("Enter a valid http or https meeting link."));
    return;
  }

  const auto event = collectEventData();
  pcm::meeting::copyMeetingInvite(QString::fromStdString(event.meeting_url),
                                  selectedClientName(),
                                  event.start_date.value_or(0));
}

void QEventDetailsWidget::onTimeFromChanged(const QTime &timeFrom) {
  if (mUI->mTimeTo->time() < timeFrom) {
    mUI->mTimeTo->setTime(timeFrom.addSecs(60)); // At least 1 minute
  }
  updateButtonState();
  updateConflictWarning();
}

void QEventDetailsWidget::onTimeToChanged(const QTime &timeTo) {
  if (timeTo < mUI->mTimeFrom->time()) {
    mUI->mTimeFrom->setTime(timeTo.addSecs(-60));
  }
  updateButtonState();
  updateConflictWarning();
}

void QEventDetailsWidget::updateButtonState() const {
  const bool isValid = !mUI->mTitle->text().trimmed().isEmpty() &&
                       mUI->mTimeTo->time() > mUI->mTimeFrom->time();
  if (auto *applyButton = mUI->mButtonBox->button(QDialogButtonBox::Apply)) {
    applyButton->setEnabled(isValid && !mPendingMeetingCreation);
  }

  if (mSeriesBacked) {
    // The occurrence is joined by its series target; the permanent invitation
    // lives in secure storage and is read when one of the copy buttons is used.
    mOpenMeetingButton->setEnabled(!mSeriesJoinTarget.isEmpty());
    mCopyMeetingUrlButton->setEnabled(mSeriesLinkReady);
    mCopyMeetingInviteButton->setEnabled(mSeriesLinkReady);
    mCopyMeetingPasscodeButton->setEnabled(mSeriesLinkReady);
    return;
  }

  if (mCurrentEvent &&
      mCurrentEvent->providerKind() == pcm::meeting::ProviderKind::LiveKit) {
    // Open joins a LiveKit meeting natively by its meetingRef. Its invitation
    // URL is still shareable with the participant; the passcode is delivered
    // through the separate channel kept in invitationState.
    mOpenMeetingButton->setEnabled(!mCurrentEvent->meetingRef().isEmpty());
    const bool hasValidMeetingUrl = pcm::meeting::isValidInvitationUrl(mMeetingUrlEdit->text());
    mCopyMeetingUrlButton->setEnabled(hasValidMeetingUrl);
    mCopyMeetingInviteButton->setEnabled(hasValidMeetingUrl);
    return;
  }

  const bool hasValidMeetingUrl = pcm::meeting::isValidMeetingUrl(mMeetingUrlEdit->text());
  mOpenMeetingButton->setEnabled(hasValidMeetingUrl);
  mCopyMeetingUrlButton->setEnabled(hasValidMeetingUrl);
  mCopyMeetingInviteButton->setEnabled(hasValidMeetingUrl);
}

void QEventDetailsWidget::updateCancellationControls() const {
  const auto statusId = mUI->mEventStatusComboBox->currentData().toLongLong();
  const bool isCanceled = statusId == 3 || statusId == 5;
  mUI->mCancellationReasonLabel->setVisible(isCanceled);
  mUI->mCancellationReasonEdit->setVisible(isCanceled);
  mUI->mCanceledByLabel->setVisible(isCanceled);
  mUI->mCanceledByComboBox->setVisible(isCanceled);
}

void QEventDetailsWidget::updateRecurringControls() const {
  const bool canConfigureRecurrence = mInEditMode;
  mRepeatTypeControl->setVisible(canConfigureRecurrence);
  if (auto *label = mUI->formLayout->labelForField(mRepeatTypeControl)) {
    label->setVisible(canConfigureRecurrence);
  }
  mRecurringOptionsWidget->setVisible(canConfigureRecurrence &&
                                      isRecurring());
  mWeekdayOptionsWidget->setVisible(canConfigureRecurrence &&
                                    mRepeatTypeControl->currentData().toString() ==
                                        QLatin1String("weekly"));
  if (auto *label = mUI->formLayout->labelForField(mWeekdayOptionsWidget)) {
    label->setVisible(mWeekdayOptionsWidget->isVisible());
  }
  mRepeatUntilDateEdit->setEnabled(mRepeatUntilSwitch->isChecked());
}

void QEventDetailsWidget::selectWeekday(const int dayOfWeek, const bool checked) {
  for (auto *button : mWeekdayButtons) {
    if (!button) {
      continue;
    }
    button->setChecked(button->property("dayOfWeek").toInt() == dayOfWeek && checked);
  }
}

QString QEventDetailsWidget::selectedWeekdayRule() const {
  QStringList days;
  for (const auto *button : mWeekdayButtons) {
    if (!button || !button->isChecked()) {
      continue;
    }
    days.append(weekdayToken(button->property("dayOfWeek").toInt()));
  }
  return days.join(',');
}

bool QEventDetailsWidget::validateInput() {
  const bool isTitleEmpty = mUI->mTitle->text().trimmed().isEmpty();
  const bool isZeroDuration = mUI->mTimeTo->time() <= mUI->mTimeFrom->time();

  if (isTitleEmpty) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr(": EVENT_TITLE_EMPTY_ERROR"));
    return false;
  }
  if (isZeroDuration) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr(": EVENT_DURATION_INVALID_ERROR"));
    return false;
  }
  // The meeting link only applies to the external-link provider; a LiveKit
  // meeting is created by the backend and has no link to enter.
  const bool wantsLiveKit = mProviderKindControl->currentIndex() == 1;
  if (mOnlineSessionSwitch->isChecked() && !wantsLiveKit) {
    const auto meetingUrl = mMeetingUrlEdit->text().trimmed();
    if (meetingUrl.isEmpty()) {
      const auto answer = QMessageBox::question(
          this, tr("Online session"),
          tr("Online session is enabled, but the meeting link is empty. Save without a link?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) {
        return false;
      }
    } else if (!pcm::meeting::isValidMeetingUrl(meetingUrl)) {
      QMessageBox::warning(this, tr(": ERROR_TITLE"),
                           tr("Enter a valid http or https meeting link."));
      return false;
    }
  }
  if (isRecurring() && mRepeatUntilSwitch->isChecked() &&
      mRepeatUntilDateEdit->date() < mUI->mEventDate->date()) {
    QMessageBox::warning(this, tr(": ERROR_TITLE"),
                         tr("Repeat end date must not be earlier than the event date."));
    return false;
  }
  return true;
}

bool QEventDetailsWidget::updateMeetingViaCoordinator() {
  if (!mCurrentEvent) {
    return true;
  }

  const bool isOnline = mOnlineSessionSwitch->isChecked();

  if (!isOnline) {
    // A LiveKit meeting is invalidated once the offline event is saved
    // (finishApply); an external link has nothing to cancel.
    supersedeCurrentLiveKitMeeting();
    applyProviderFields(std::nullopt, QString{}, std::nullopt, QString{});
    return true;
  }

  const bool wantsSeriesLiveKit = mProviderKindControl->currentIndex() == 1;
  if (mSeriesBacked ||
      (wantsSeriesLiveKit && mCreatingNewEvent && isRecurring() && !mLegacyMigratable)) {
    // The call belongs to a recurring SERIES (existing, or about to be created
    // by the owner of this form): one permanent invitation for all dates, rooms
    // created per date by the server. Never create or replace a single meeting
    // here - that would copy one scheduled room to every occurrence.
    applyProviderFields(pcm::meeting::ProviderKind::LiveKit, QString{}, std::nullopt, QString{});
    return true;
  }

  if (!mMeetingCoordinator) {
    // Defensive fallback (should not happen once Task 7 wires the coordinator
    // everywhere QEventDetailsWidget is constructed): preserve today's
    // behavior instead of silently dropping the link.
    const auto trimmedUrl = mMeetingUrlEdit->text().trimmed();
    applyProviderFields(pcm::meeting::ProviderKind::ExternalUrl, trimmedUrl,
                       mCurrentEvent->invitationState(), trimmedUrl);
    return true;
  }

  const bool hasLiveKitMeeting =
      mCurrentEvent->providerKind() == pcm::meeting::ProviderKind::LiveKit &&
      !mCurrentEvent->meetingRef().isEmpty();
  const bool wantsLiveKit = mProviderKindControl->currentIndex() == 1;
  if (wantsLiveKit) {
    // onApplyClicked has already written the form's schedule to mCurrentEvent.
    const auto start = mCurrentEvent->getStartTime();
    const auto end = mCurrentEvent->getEndTime();
    if (hasLiveKitMeeting && start == mMeetingScheduledStart && end == mMeetingScheduledEnd) {
      // Nothing meeting-relevant changed (e.g. only the title was edited):
      // keep the existing meeting, so the invitation already sent to the
      // client stays valid and no backend meeting is orphaned.
      return true;
    }

    // The existing meeting's schedule no longer matches: replace it. The old
    // one is invalidated only after the replacement has been saved.
    supersedeCurrentLiveKitMeeting();

    // LiveKitMeetingProvider::create makes a real, asynchronous POST
    // /v1/meetings call (unlike ExternalUrlMeetingProvider::create below), so
    // the save waits for onMeetingCreated / onMeetingCreateFailed.
    // mPendingMeetingCreation is set before the call so the result is handled
    // correctly even if a provider ever delivered it synchronously.
    mPendingMeetingCreation = true;
    mMeetingCoordinator->createMeeting(
        pcm::meeting::ProviderKind::LiveKit,
        {.scheduledStartIso = start.toUTC().toString(Qt::ISODate),
         .scheduledEndIso = end.toUTC().toString(Qt::ISODate)});
    return false;
  }

  // Switching a LiveKit event to an external link drops its LiveKit meeting.
  if (hasLiveKitMeeting) {
    supersedeCurrentLiveKitMeeting();
  }

  // ExternalUrlMeetingProvider::create emits `created` synchronously (Task 2),
  // and onMeetingCreated (connected once, in setMeetingCoordinator) applies the
  // descriptor to mCurrentEvent before this call returns.
  mAwaitingSyncMeetingResult = true;
  mMeetingCoordinator->createMeeting(pcm::meeting::ProviderKind::ExternalUrl,
                                     {.rawMeetingUrl = mMeetingUrlEdit->text()});
  mAwaitingSyncMeetingResult = false;
  return true;
}

void QEventDetailsWidget::supersedeCurrentLiveKitMeeting() {
  if (!mCurrentEvent ||
      mCurrentEvent->providerKind() != pcm::meeting::ProviderKind::LiveKit ||
      mCurrentEvent->meetingRef().isEmpty()) {
    return;
  }
  // Keep the first one since the last save: that is the meeting the stored
  // event still references.
  if (mSupersededLiveKitMeetingRef.isEmpty()) {
    mSupersededLiveKitMeetingRef = mCurrentEvent->meetingRef();
  }
}

void QEventDetailsWidget::cancelSupersededLiveKitMeeting() {
  const auto supersededRef = std::exchange(mSupersededLiveKitMeetingRef, QString{});
  if (supersededRef.isEmpty() || !mMeetingCoordinator) {
    return;
  }
  // A failed replacement leaves the old meeting as the event's meeting.
  if (mCurrentEvent &&
      mCurrentEvent->providerKind() == pcm::meeting::ProviderKind::LiveKit &&
      mCurrentEvent->meetingRef() == supersededRef) {
    return;
  }
  // Fire-and-forget: the event no longer references this meeting, and a
  // failed invalidate carries no state the UI needs to react to.
  mMeetingCoordinator->cancelMeeting(pcm::meeting::ProviderKind::LiveKit, supersededRef);
}

DuckEvent QEventDetailsWidget::collectEventData() const {
  // This method is no longer used in the main logic flow.
  // It is left for backward compatibility or internal use if needed.
  DuckEvent event;
  event.name = mUI->mTitle->text().toStdString();
  event.start_date = QDateTime(mUI->mEventDate->date(), mUI->mTimeFrom->time(),
                               QTimeZone::systemTimeZone())
                         .toMSecsSinceEpoch();
  event.end_date = QDateTime(mUI->mEventDate->date(), mUI->mTimeTo->time(),
                             QTimeZone::systemTimeZone())
                       .toMSecsSinceEpoch();
  event.is_work_event = mEventTypeSwitch->isChecked();
  event.duration = (event.end_date.value_or(0) - event.start_date.value_or(0)) / 1000; // in seconds
  event.cost = event.is_work_event ? std::make_optional(mUI->mCostSpinBox->value())
                                   : std::nullopt;
  event.payment_stat_id =
      event.is_work_event ? mUI->mPaymentStatusComboBox->currentData().toLongLong()
                          : kPaymentSkippedId;
  event.is_online = mOnlineSessionSwitch->isChecked();
  event.meeting_url =
      event.is_online ? mMeetingUrlEdit->text().trimmed().toStdString() : std::string{};
  event.buffer_before_minutes = mBufferBeforeSpinBox->value();
  event.buffer_after_minutes = mBufferAfterSpinBox->value();
  return event;
}

DuckEvent QEventDetailsWidget::liveCandidateEvent() const {
  DuckEvent event;
  if (mCurrentEvent) {
    event.id = mCurrentEvent->getId();
    const auto persisted = mCurrentEvent->toEvent();
    event.series_id = persisted.series_id;
    event.original_occurrence_start = persisted.original_occurrence_start;
  }
  event.start_date = QDateTime(mUI->mEventDate->date(), mUI->mTimeFrom->time(),
                               QTimeZone::systemTimeZone())
                         .toMSecsSinceEpoch();
  event.end_date = QDateTime(mUI->mEventDate->date(), mUI->mTimeTo->time(),
                             QTimeZone::systemTimeZone())
                       .toMSecsSinceEpoch();
  event.buffer_before_minutes = mBufferBeforeSpinBox->value();
  event.buffer_after_minutes = mBufferAfterSpinBox->value();
  return event;
}

QString QEventDetailsWidget::conflictWarningText(const DuckEvent &conflict) const {
  const auto start = QDateTime::fromMSecsSinceEpoch(conflict.start_date.value_or(0),
                                                     QTimeZone::UTC)
                         .toLocalTime();
  const auto end = QDateTime::fromMSecsSinceEpoch(conflict.end_date.value_or(0),
                                                   QTimeZone::UTC)
                       .toLocalTime();
  const QString timeRange = QStringLiteral("%1–%2").arg(
      start.toString(QStringLiteral("HH:mm")), end.toString(QStringLiteral("HH:mm")));
  const QString name =
      QString::fromStdString(conflict.name.value_or(std::string())).trimmed();
  if (name.isEmpty()) {
    return tr("Overlaps with an existing event, %1.").arg(timeRange);
  }
  return tr("Overlaps with \"%1\", %2.").arg(name, timeRange);
}

void QEventDetailsWidget::updateConflictWarning() {
  if (!mConflictWarningLabel || !mSuggestFreeSlotButton) {
    return;
  }

  std::optional<DuckEvent> conflict;
  if (mConflictChecker) {
    const auto candidate = liveCandidateEvent();
    if (candidate.start_date.has_value() && candidate.end_date.has_value() &&
        *candidate.end_date > *candidate.start_date) {
      conflict = mConflictChecker(candidate);
    }
  }

  mConflictWarningLabel->setVisible(conflict.has_value());
  mSuggestFreeSlotButton->setVisible(conflict.has_value());
  if (conflict.has_value()) {
    mConflictWarningLabel->setText(conflictWarningText(*conflict));
  }
}

void QEventDetailsWidget::onSuggestFreeSlotClicked() {
  if (!mConflictChecker) {
    return;
  }

  auto candidate = liveCandidateEvent();
  if (!candidate.start_date.has_value() || !candidate.end_date.has_value()) {
    return;
  }
  const auto durationMs = *candidate.end_date - *candidate.start_date;
  const auto dayEndMs = QDateTime(mUI->mEventDate->date(), pcm::app_settings::workDayEnd(),
                                  QTimeZone::systemTimeZone())
                            .toMSecsSinceEpoch();

  auto cursor = *candidate.start_date;
  // Bounded walk: each iteration jumps past the conflict it just found, so
  // this terminates in at most as many steps as there are events that day.
  for (int iteration = 0; iteration < 100; ++iteration) {
    candidate.start_date = cursor;
    candidate.end_date = cursor + durationMs;
    if (*candidate.end_date > dayEndMs) {
      return; // No room left in the work day.
    }

    const auto conflict = mConflictChecker(candidate);
    if (!conflict.has_value()) {
      const auto newStart =
          QDateTime::fromMSecsSinceEpoch(cursor, QTimeZone::UTC).toLocalTime();
      const auto newEnd =
          QDateTime::fromMSecsSinceEpoch(*candidate.end_date, QTimeZone::UTC).toLocalTime();
      mUI->mTimeFrom->setTime(newStart.time());
      mUI->mTimeTo->setTime(newEnd.time());
      return;
    }

    const auto conflictEnd = conflict->end_date.value_or(cursor + durationMs);
    cursor = conflictEnd + conflict->buffer_after_minutes * 60'000;
  }
}

void QEventDetailsWidget::onCopyMeetingPasscodeClicked() {
  withSeriesInvitation([](const QString &, const QString &passcode) {
    pcm::clipboard::copySensitiveText(passcode);
  });
}

void QEventDetailsWidget::withSeriesInvitation(
    const std::function<void(const QString &url, const QString &passcode)> &use) {
  if (!mSeriesCalls) {
    return;
  }
  // The secret is read from secure storage only now, asynchronously, and handed
  // to the clipboard helper; it is never kept in this widget.
  QPointer<QEventDetailsWidget> guard(this);
  mSeriesCalls->loadInvitation(mSeriesId, [guard, use](const bool ok, const QString &url,
                                                       const QString &passcode) {
    if (!guard) {
      return;
    }
    if (!ok) {
      QMessageBox::warning(guard, tr(": ERROR_TITLE"),
                           tr("The system keychain is not available, so the link cannot be read."));
      return;
    }
    if (url.isEmpty()) {
      QMessageBox::warning(guard, tr(": ERROR_TITLE"),
                           tr("The permanent link is not stored on this device. Create a new link."));
      return;
    }
    use(url, passcode);
  });
}

void QEventDetailsWidget::setSeriesCall(pcm::meeting::SeriesCallService *service,
                                        const int64_t seriesId, const QString &joinTarget) {
  if (mSeriesCalls) {
    disconnect(mSeriesCalls, &pcm::meeting::SeriesCallService::statusChanged, this, nullptr);
  }
  mSeriesCalls = service;
  mSeriesId = seriesId;
  mSeriesJoinTarget = joinTarget;
  mSeriesBacked = service != nullptr && seriesId > 0;
  mLegacyMigratable = false;
  if (mSeriesCalls) {
    connect(mSeriesCalls, &pcm::meeting::SeriesCallService::statusChanged, this,
            [this](const qint64 changed) {
              if (changed == mSeriesId) {
                refreshSeriesCallPanel();
              }
            });
  }
  applySeriesCallState();
}

void QEventDetailsWidget::setLegacyMigration(pcm::meeting::SeriesCallService *service,
                                             const int64_t seriesId) {
  setSeriesCall(nullptr, 0, {});
  mSeriesCalls = service;
  mSeriesId = seriesId;
  mLegacyMigratable = service != nullptr && seriesId > 0;
  if (mSeriesCalls) {
    connect(mSeriesCalls, &pcm::meeting::SeriesCallService::statusChanged, this,
            [this](const qint64 changed) {
              if (changed == mSeriesId) {
                refreshSeriesCallPanel();
              }
            });
  }
  applySeriesCallState();
}

void QEventDetailsWidget::applySeriesCallState() {
  // A series-backed call is the series' business: the online/provider choice
  // is locked so a stray click cannot detach one occurrence from its series.
  mOnlineSessionSwitch->setEnabled(!mSeriesBacked);
  mProviderKindControl->setEnabled(!mSeriesBacked);
  mMeetingUrlEdit->setReadOnly(mSeriesBacked);
  mCopyMeetingPasscodeButton->setVisible(mSeriesBacked);
  if (mSeriesBacked) {
    mMeetingUrlEdit->setText(QString{});
    mMeetingUrlEdit->setPlaceholderText(
        tr("The permanent link is stored securely. Use Copy link."));
  } else {
    mMeetingUrlEdit->setPlaceholderText(tr("https://..."));
  }
  refreshSeriesCallPanel();
}

void QEventDetailsWidget::refreshSeriesCallPanel() {
  const bool relevant = mSeriesCalls && (mSeriesBacked || mLegacyMigratable);
  pcm::eventpage::SeriesStatusView view;
  if (relevant) {
    view = pcm::eventpage::describeSeriesCallStatus(mSeriesCalls->status(mSeriesId));
  }
  const bool showPanel = relevant && (mSeriesBacked || !view.lines.isEmpty() || mLegacyMigratable);
  mSeriesCallWidget->setVisible(showPanel);
  mSeriesStatusLabel->setText(view.lines.join(QLatin1Char('\n')));
  mSeriesStatusLabel->setVisible(!view.lines.isEmpty());
  const char *color = view.severity == pcm::eventpage::StatusSeverity::Error     ? "#ff8a80"
                      : view.severity == pcm::eventpage::StatusSeverity::Warning ? "#f0c36d"
                                                                                 : "#9aa4b2";
  mSeriesStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(color)));
  mSeriesRetryButton->setVisible(relevant && view.canRetry);
  mSeriesReissueButton->setVisible(relevant && view.canReissue);
  mSeriesPublishButton->setVisible(relevant && view.canPublishThisDevice);
  mMigrateButton->setVisible(relevant && mLegacyMigratable &&
                             mSeriesCalls->status(mSeriesId).migration.state !=
                                 pcm::meeting::MigrationState::Publishing &&
                             mSeriesCalls->status(mSeriesId).migration.state !=
                                 pcm::meeting::MigrationState::CreatingInvitation);
  mSeriesLinkReady = relevant && view.linkReady;
  updateButtonState();
}
