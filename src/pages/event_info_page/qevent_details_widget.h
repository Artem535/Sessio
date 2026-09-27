#pragma once

#include "event_item.h"
#include "meeting_coordinator.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
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
  void setConflictChecker(
      std::function<std::optional<DuckEvent>(const DuckEvent &)> checker);
  void setMeetingCoordinator(pcm::meeting::MeetingCoordinator *coordinator);

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
  void onOpenMeetingClicked();
  void onCopyMeetingUrlClicked();
  void onCopyMeetingInviteClicked();
  void onTimeFromChanged(const QTime &timeFrom);
  void onTimeToChanged(const QTime &timeTo);
  void onSuggestFreeSlotClicked();

private:
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
  void updateMeetingViaCoordinator();
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
  std::function<std::optional<DuckEvent>(const DuckEvent &)> mConflictChecker;
  QPointer<pcm::meeting::MeetingCoordinator> mMeetingCoordinator;
};
