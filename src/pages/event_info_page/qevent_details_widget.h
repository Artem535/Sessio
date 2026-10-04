#pragma once

#include "event_item.h"
#include "meeting_coordinator.h"
#include "series_call_service.h"

#include <QComboBox>
#include <QBoxLayout>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QWidget>
#include <functional>
#include <memory>

namespace oclero::qlementine {
class LineEdit;
class SegmentedControl;
class Switch;
}

// Forward declaration for UI
namespace Ui {
class EventDetails;
}

/**
 * @brief A widget for displaying and editing event details.
 * Contains all input fields and buttons for creating/modifying an event.
 * Does not own the database; receives client data externally.
 */
class QEventDetailsWidget final : public QWidget {
  Q_OBJECT

public:
  explicit QEventDetailsWidget(QWidget *parent = nullptr);
  ~QEventDetailsWidget() override;

  /**
   * @brief Loads data from an existing event into the form for editing.
   * @param event Pointer to the event.
   */
  void loadEvent(QEventItem *event,
                 std::optional<int64_t> clientId = std::nullopt);

  /**
   * @brief Loads data from an existing event and enters edit mode.
   */
  void startEditingEvent(QEventItem *event,
                         std::optional<int64_t> clientId = std::nullopt);

  /**
   * @brief Clears the form and switches to "create new event" mode.
   */
  void startCreatingNewEvent(const QDate &date = QDate::currentDate(),
                             std::optional<QTime> startTime = std::nullopt,
                             std::optional<int> durationMinutes = std::nullopt);

  /**
   * @brief Configures widget for usage inside a modal dialog.
   */
  void setDialogMode(bool enabled);
  void setInspectorMode(bool enabled);
  void setConflictChecker(
      std::function<std::optional<DuckEvent>(const DuckEvent &)> checker);
  void setMeetingCoordinator(pcm::meeting::MeetingCoordinator *coordinator);

  /**
   * @brief Marks the edited event as an occurrence of a published recurring
   * call series. Its call is owned by the series: no single meeting is ever
   * created or replaced for it, the online/provider choice is locked, Open
   * joins the occurrence by `joinTarget`, Copy link/invite/passcode read the
   * series' permanent invitation from secure storage, and sync / invitation
   * state is shown inline with manual retry.
   */
  void setSeriesCall(pcm::meeting::SeriesCallService *service, int64_t seriesId,
                     const QString &joinTarget);
  /**
   * @brief Marks the edited event as part of a legacy LiveKit series (one
   * shared meeting) and offers the opt-in move to a permanent series link.
   */
  void setLegacyMigration(pcm::meeting::SeriesCallService *service, int64_t seriesId);
  [[nodiscard]] bool isSeriesBacked() const { return mSeriesBacked; }

  /**
   * @brief Checks if the widget is in edit mode.
   * @return true if in edit mode, false otherwise.
   */
  [[nodiscard]] bool isInEditMode() const;

  /**
   * @brief Checks if a new event is being created.
   * @return true if creating a new event, false otherwise.
   */
  [[nodiscard]] bool isCreatingNewEvent() const;
  [[nodiscard]] int64_t selectedClientId() const;
  [[nodiscard]] QString selectedClientName() const;
  [[nodiscard]] bool isRecurring() const;
  // True when saving this form creates a NEW recurring series that wants the
  // LiveKit provider (the owner then publishes it and requests the permanent
  // invitation instead of the form creating a single meeting).
  [[nodiscard]] bool wantsNewLiveKitSeries() const;
  [[nodiscard]] QString recurrenceRule() const;
  [[nodiscard]] std::optional<int64_t> recurrenceUntilMs() const;
  void setRecurrenceRule(const QString &rule,
                         std::optional<int64_t> recurrenceUntilMs = std::nullopt);
  void rejectPendingSave();

  /**
   * @brief Returns a pointer to the current event (may be nullptr if creating a
   * new one).
   * @return Pointer to the event.
   */
  [[nodiscard]] QEventItem *currentEvent() const;

  /**
   * @brief Sets the list of clients available for selection.
   * @param clients A hash map where key is client ID and value is client
   * display name.
   */
  void setClientList(const QHash<int64_t, QString> &clients);

signals:
  // Inspector mode: "Back to day" was pressed.
  void backRequested();
  void editRequested();
  /**
   * @brief Signal emitted when user requests to save the event.
   * Passes a pointer to the event data that should be saved.
   */
  void provideEventSave(QEventItem *event);
  void provideDialogAccept();

  /**
   * @brief Signal emitted when user cancels editing/creation.
   */
  void provideEditingCanceled();

  /**
   * @brief Signal emitted when the edit mode changes.
   */
  void provideEditModeChanged();

  void provideFillClientComboBox(QComboBox *combobox);

  /**
   * @brief Emitted when the user clicks "Open Meeting" on a LiveKit-provider
   * event, instead of trying to open a URL.
   */
  void openLiveKitMeetingRequested(QString meetingRef);

  /**
   * @brief The user asked to move this legacy LiveKit series to a permanent
   * link; the owner confirms the series' timezone and starts the migration.
   */
  void migrateToPermanentLinkRequested(qint64 seriesId);

private slots:
  // --- Button Slots ---
  void onApplyClicked();
  void onCancelClicked();
  void onAddClicked();
  void onChangeClicked();

  // --- Input Change Slots ---
  void onEventTypeToggled(bool checked);
  void onOnlineSessionToggled(bool checked);
  void onRecurrenceTypeChanged();
  void onMeetingUrlChanged(const QString &url);
  void onMeetingCreated(pcm::meeting::MeetingDescriptor descriptor);
  void onMeetingCreateFailed(const QString &error);
  void onOpenMeetingClicked();
  void onCopyMeetingUrlClicked();
  void onCopyMeetingInviteClicked();
  void onTimeFromChanged(const QTime &timeFrom);
  void onTimeToChanged(const QTime &timeTo);
  void onSuggestFreeSlotClicked();
  void onCopyMeetingPasscodeClicked();

private:
  void refreshInspector();
  void setMeetingActionOrder(const QList<QPushButton *> &order);
  void applySeriesCallState();
  void refreshSeriesCallPanel();
  void withSeriesInvitation(const std::function<void(const QString &url, const QString &passcode)> &use);

  // --- Initialization ---
  void initUi();
  void initConnections();
  void initDefaultStyle();
  void initEditStyle();
  void initDefaultStates() const;
  void initDefaultTimes() const;
  void updateButtonState() const;
  void updateCancellationControls() const;
  void updateRecurringControls() const;
  void selectWeekday(int dayOfWeek, bool checked);
  [[nodiscard]] QString selectedWeekdayRule() const;

  // --- Validation & Data Collection ---
  bool validateInput();
  [[nodiscard]] DuckEvent collectEventData() const;
  /**
   * @brief Brings mCurrentEvent's meeting in line with the form's online/provider
   * selection.
   * @return true when the meeting fields are settled and the caller may save
   * right away; false when an asynchronous LiveKit create is in flight and
   * onMeetingCreated/onMeetingCreateFailed will finish (or abort) the apply.
   */
  [[nodiscard]] bool updateMeetingViaCoordinator();
  /**
   * @brief Second half of an apply: emits the save and leaves edit mode.
   * Runs synchronously from onApplyClicked, or from onMeetingCreated once an
   * asynchronous LiveKit create has completed.
   */
  void finishApply(bool isCreatingNewEvent);
  void supersedeCurrentLiveKitMeeting();
  void cancelSupersededLiveKitMeeting();
  void applyProviderFields(std::optional<pcm::meeting::ProviderKind> kind,
                           const QString &meetingRef,
                           const std::optional<QString> &invitationState,
                           const QString &meetingUrl);

  // --- Live conflict warning ---
  void updateConflictWarning();
  [[nodiscard]] DuckEvent liveCandidateEvent() const;
  [[nodiscard]] QString conflictWarningText(const DuckEvent &conflict) const;

  // --- UI ---
  std::unique_ptr<Ui::EventDetails> mUI;
  QWidget *mEditorFields = nullptr;
  QWidget *mInspectorSummary = nullptr;
  QBoxLayout *mSeriesActionsLayout = nullptr;
  QLabel *mInspectorTitle = nullptr;
  // Labelled read-only fields: small muted caption above the value.
  struct InspectorField {
    QLabel *caption = nullptr;
    QLabel *value = nullptr;
  };
  InspectorField mFieldClient, mFieldDate, mFieldTime, mFieldRepeat, mFieldFormat, mFieldStatus;
  QLabel *mInspectorDuration = nullptr;
  QLabel *mSeriesHint = nullptr;
  std::unique_ptr<QEventItem> mInspectorEvent;
  bool mInspectorMode = false;
  quint64 mSelectionRevision = 0;
  oclero::qlementine::Switch *mEventTypeSwitch = nullptr;
  oclero::qlementine::Switch *mOnlineSessionSwitch = nullptr;
  oclero::qlementine::SegmentedControl *mProviderKindControl = nullptr;
  oclero::qlementine::SegmentedControl *mRepeatTypeControl = nullptr;
  QWidget *mRecurringOptionsWidget = nullptr;
  QSpinBox *mRepeatIntervalSpinBox = nullptr;
  QWidget *mWeekdayOptionsWidget = nullptr;
  QVector<QPushButton *> mWeekdayButtons;
  oclero::qlementine::Switch *mRepeatUntilSwitch = nullptr;
  QDateEdit *mRepeatUntilDateEdit = nullptr;
  QLabel *mMeetingUrlLabel = nullptr;
  oclero::qlementine::LineEdit *mMeetingUrlEdit = nullptr;
  QWidget *mMeetingActionsWidget = nullptr;
  QPushButton *mOpenMeetingButton = nullptr;
  QPushButton *mCopyMeetingUrlButton = nullptr;
  QPushButton *mCopyMeetingInviteButton = nullptr;
  QPushButton *mCopyMeetingPasscodeButton = nullptr;
  QWidget *mSeriesCallWidget = nullptr;
  QLabel *mSeriesStatusLabel = nullptr;
  QPushButton *mSeriesRetryButton = nullptr;
  QPushButton *mSeriesReissueButton = nullptr;
  QPushButton *mSeriesPublishButton = nullptr;
  QPushButton *mMigrateButton = nullptr;
  QWidget *mBuffersWidget = nullptr;
  QSpinBox *mBufferBeforeSpinBox = nullptr;
  QSpinBox *mBufferAfterSpinBox = nullptr;
  QLabel *mConflictWarningLabel = nullptr;
  QPushButton *mSuggestFreeSlotButton = nullptr;

  // --- Data ---
  QPointer<QEventItem> mCurrentEvent;
  QHash<int64_t, QString> mClientList;
  bool mInEditMode = false;
  bool mCreatingNewEvent = false;
  bool mDialogMode = false;
  bool mSaveAccepted = true;
  // An apply is waiting for an asynchronous LiveKit meeting create; the save
  // is deferred until onMeetingCreated (or dropped by onMeetingCreateFailed).
  bool mPendingMeetingCreation = false;
  // Set only around the synchronous ExternalUrl createMeeting call, so a
  // meetingCreated this widget did not ask for is ignored.
  bool mAwaitingSyncMeetingResult = false;
  // LiveKit meeting replaced (or dropped) by the apply in progress. It is
  // invalidated on the backend only after the replacement has been saved, so
  // a failed create or a rejected save never leaves the stored event pointing
  // at an already-invalidated meeting.
  QString mSupersededLiveKitMeetingRef;
  // Schedule the current LiveKit meeting was created for; re-applying with an
  // unchanged schedule keeps the meeting (and the client's invitation) as is.
  QDateTime mMeetingScheduledStart;
  QDateTime mMeetingScheduledEnd;
  std::function<std::optional<DuckEvent>(const DuckEvent &)> mConflictChecker;
  QPointer<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;

  // --- Published recurring call series ---
  QPointer<pcm::meeting::SeriesCallService> mSeriesCalls;
  int64_t mSeriesId = 0;
  QString mSeriesJoinTarget;
  bool mSeriesBacked = false;      // the call is owned by a published series
  bool mLegacyMigratable = false;  // legacy LiveKit series: move to a permanent link offered
  bool mSeriesLinkReady = false;   // the permanent invitation can be copied
};
