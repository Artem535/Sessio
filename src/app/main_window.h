#pragma once

#include "analytics_page.h"
#include "calls_page.h"
#include "client_info.h"
#include "client_notes_page.h"
#include "event_info.h"
#include "database.h"
#include "client_info_card.h"
#include "meeting_coordinator.h"
#include "settings_dialog.h"
#include "tab_button.h"
#include "token_backend_client.h"

#include <QAction>
#include <QLineEdit>
#include <QPushButton>
#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>

#include <functional>
#include <memory>
#include <optional>

namespace oclero::qlementine {
class LineEdit;
class Switch;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * @brief Main application window class.
 *
 * Manages the main UI and switching between different pages like
 * client info, event info, and client card.
 */
class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Enum to identify the available pages in the application.
   */
  enum class Pages { clientInfo, eventInfo, analytics, clientCard, clientNotes, calls };

  /**
   * @brief Constructor for the MainWindow class.
   * Initializes the UI and navigation buttons.
   */
  explicit MainWindow(QWidget *parent = nullptr);

  /** @brief Destructor. */
  ~MainWindow() override;

  /**
   * @brief Adds the client information page to the application.
   * @param model Shared pointer to the client model.
   */
  void addClientInfoPage(std::shared_ptr<QClientModel> model);

  /**
   * @brief Adds the event information page to the application.
   * @param model Pointer to the timeline model.
   */
  void addEventInfoPage(QTimelineModel *model,
                        pcm::meeting::MeetingCoordinator *meetingCoordinator);
  void addAnalyticsPage(std::shared_ptr<pcm::database::Database> db);

  /**
   * @brief Adds the client card (details) page to the application.
   */
  void addClientCardPage(std::shared_ptr<pcm::database::Database> db);
  void addClientNotesPage(std::shared_ptr<pcm::database::Database> db);

  /**
   * @brief Adds the calls (video meetings) page to the application.
   * @param deviceManager Camera/microphone/speaker enumeration, owned by the caller.
   * @param tokenClient Token-backend HTTP client, owned by the caller.
   * @param bearerCredentialProvider Supplies the specialist bearer credential on demand.
   */
  void addCallsPage(pcm::video::DeviceManager *deviceManager,
                    pcm::tokenclient::TokenBackendClient *tokenClient,
                    std::function<QString()> bearerCredentialProvider);
  void setDatabase(std::shared_ptr<pcm::database::Database> db);

  /**
   * @brief Sets up all signal/slot connections between UI elements and logic.
   */
  void connectSignals();

  /**
   * @brief Returns a pointer to the specified page widget.
   * @param page The page enum value.
   * @return QWidget* Pointer to the corresponding page.
   */
  QWidget* getPage(Pages page);

  /**
   * @brief Sets an optional custom widget for the top-right grid cell.
   * @param page The page to bind the widget to.
   * @param widget The widget to show for that page (can be nullptr).
   */
  void setPageCustomWidget(Pages page, QWidget *widget);

  /**
   * @brief Switches to the Calls tab and preselects the given LiveKit
   * meeting, in response to "Open Meeting" being clicked on a LiveKit-
   * provider event.
   * @param meetingRef The meeting reference to preselect on the Calls page.
   */
  void preselectLiveKitMeeting(const QString &meetingRef);

signals:
  /**
   * @brief Emitted when a client should be saved.
   * @param client The client data to save.
   */
  void provideSaveClient(const DuckClient &client);
  void provideRemoveClient(int64_t clientId);

  /**
   * @brief Emitted when a client-event association should be saved.
   * @param clientId ID of the client.
   * @param eventId ID of the event.
   */
  void provideClientEventPairSave(const int64_t clientId, const int64_t eventId);

  /**
   * @brief Emitted after the Settings dialog closes. Its sections save
   * independently (e.g. the LiveKit section's own Save button), so this fires
   * unconditionally and listeners re-read whatever they depend on.
   */
  void settingsSaved();

private:
  // Map of pages by type
  QHash<Pages, QWidget*> mPages;
  QHash<Pages, int> mPagesIndex;

  // UI manager
  std::unique_ptr<Ui::MainWindow> mUi;

  // Database connection, used by SettingsDialog for backup/validate actions.
  std::shared_ptr<pcm::database::Database> mDb{nullptr};

  // Navigation buttons
  TabButton *mBtnCalendar{nullptr};
  TabButton *mBtnClients{nullptr};
  TabButton *mBtnAnalytics{nullptr};
  TabButton *mBtnProfile{nullptr};
  TabButton *mBtnNotes{nullptr};
  TabButton *mBtnCalls{nullptr};
  QWidget *mClientPageActions{nullptr};
  oclero::qlementine::LineEdit *mClientSearchInput{nullptr};
  oclero::qlementine::Switch *mShowInactiveClientsSwitch{nullptr};
  QPushButton *mAddClientButton{nullptr};
  QPushButton *mBtnBackToClients{nullptr};
  QPushButton *mBtnSettings{nullptr};
  QPushButton *mBtnAbout{nullptr};
  QHBoxLayout *mPageCustomWidgetLayout{nullptr};
  QHash<Pages, QWidget*> mPageCustomWidgets;
  Pages mCurrentPage{Pages::eventInfo};

  /**
   * @brief Initializes default UI style (colors, fonts, etc.).
   */
  void initDefaultStyle() const;

  /**
   * @brief Highlights the currently active navigation button.
   * @param btn The button to mark as active.
   */
  void checkButton(QPushButton *btn) const;

  /**
   * @brief Switches current page and synchronizes all related UI areas.
   * @param page Target page.
   * @param btn Navigation button associated with the page.
   */
  void showPage(Pages page, QPushButton *btn);

  /**
   * @brief Applies page-specific widget to the top-right grid cell.
   * @param page Target page.
   */
  void applyPageCustomWidget(Pages page);
  void setClientNavigationVisible(Pages page, bool visible) const;
  void setupUtilityButtons();
  void openSettingsDialog();
  void openAboutDialog();
  [[nodiscard]] QString pageTitle(Pages page) const;
  void refreshPageAppearance();
};
