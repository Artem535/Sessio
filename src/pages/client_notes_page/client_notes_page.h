#pragma once

#include "database.h"
#include "recurrence_utils.h"

#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariant>
#include <QVector>
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

class QGridLayout;
class QTextBrowser;
class QResizeEvent;
class QSplitter;
class QLineEdit;
class QComboBox;
class QQuickWidget;

namespace oclero::qlementine {
class SegmentedControl;
}

class ClientNotesPage final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(QVariantList participants READ participants NOTIFY workspaceChanged)
  Q_PROPERTY(QVariantList journal READ journal NOTIFY workspaceChanged)
  Q_PROPERTY(QVariantMap document READ workspaceDocument NOTIFY workspaceChanged)
  Q_PROPERTY(QString participantId READ participantId NOTIFY workspaceChanged)
  Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
  Q_PROPERTY(QStringList pendingFiles READ pendingFiles NOTIFY draftChanged)
  Q_PROPERTY(QString status READ workspaceStatus NOTIFY draftChanged)

public:
  // Call-owned storage is supplied by the host; this page never invents client IDs.
  struct CallNoteActions {
    std::function<QJsonArray()> entries;
    std::function<QString(const QString &, const QStringList &, const QString &)> save;
    std::function<QString(const QString &, const QString &)> attachmentPath;
    std::function<QString()> manualText;
    std::function<bool(const QString &)> setManualText;
    std::function<QStringList()> draftAttachmentPaths;
    std::function<bool(const QStringList &)> setDraftAttachmentPaths;
  };
  explicit ClientNotesPage(std::shared_ptr<pcm::database::Database> db,
                           QWidget *parent = nullptr);
  ~ClientNotesPage() override;
  void setCallNoteActions(qint64 materialId, CallNoteActions actions);
  // Shows only this meeting's composer and entries; an outer scroll area scrolls the page.
  void setEmbedded(bool embedded);
  void focusComposer();
  void addPendingAttachments(const QStringList &paths);
  QString journalMarkdown() const;
  void setMaterialAttachmentResolver(std::function<QString(qint64, const QString &, const QString &)> resolver);
  void enableQuickWorkspace();
  QWidget *headerControls();
  QVariantList participants() const;
  QVariantList journal() const;
  QVariantMap workspaceDocument() const;
  QString participantId() const;
  QString draft() const;
  QStringList pendingFiles() const;
  QString workspaceStatus() const;
  Q_INVOKABLE void selectParticipant(const QString &id);
  Q_INVOKABLE void selectJournalEntry(const QString &key);
  Q_INVOKABLE void searchJournal(const QString &query, int filter);
  Q_INVOKABLE void setDraft(const QString &text);
  Q_INVOKABLE void saveWorkspaceNote();
  Q_INVOKABLE void attachWorkspaceFiles();
  Q_INVOKABLE void linkWorkspaceSession();
  Q_INVOKABLE void openWorkspaceMeeting(const QString &id = {});
  Q_INVOKABLE void openWorkspaceTranscript(const QString &id = {});
  Q_INVOKABLE void openWorkspaceClient();
  Q_INVOKABLE void openWorkspaceAttachment(int index);
  Q_INVOKABLE void removeWorkspaceAttachment(int index);
  Q_INVOKABLE void exportWorkspaceJournal();

signals:
  void workspaceChanged();
  void draftChanged();
  void openClientCardRequested(const std::optional<DuckClient> &client);
  void openEventRequested(int64_t eventId, qint64 dayMs);
  void noteEntrySaved(QString entryId);
  void openTranscriptRequested(qint64 transcriptId);
  void openCallMaterialRequested(qint64 materialId);

public slots:
  void setClientInfo(const std::optional<DuckClient> &client);
  void refresh();

private slots:
  void onAddNoteClicked();
  void onAttachFilesClicked();
  void onPendingAttachmentActivated(QListWidgetItem *item);
  void onOpenClientCardClicked();
  void onFeedFilterChanged();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  enum class FeedFilter { All, Sessions, Notes };

  struct PendingAttachment {
    QString sourcePath;
    QString fileName;
    QString mimeType;
    bool isImage = false;
  };

  void buildUi();
  void buildJournal();
  void setJournalMode(bool enabled);
  void reloadJournal();
  void filterJournal();
  void showJournalEntry();
  void arrangeCompactUi();
  void reloadCallEntries();
  void addCallEntry(const QJsonObject &entry);
  void updatePreview();
  void restoreCallDraft();
  void loadPendingAttachmentPaths(const QStringList &paths);
  bool saveDraftAttachmentPaths();
  void reloadNotes();
  void clearNotes();
  void addNoteBubble(const DuckClientNote &note);
  void addSessionEntry(const DuckEvent &event);
  void addChangeLogEntry(const DuckEventChangeLog &entry);
  void addEventLink(QVBoxLayout *layout, QWidget *bubble, const DuckEvent &event);
  // Read-only whole-meeting AI summary of a call; edited from the call itself.
  void addAiSummaryEntry(qint64 timestampMs, std::optional<int64_t> eventId, const QString &markdown);
  QLabel *addDateDivider(const QDate &date);
  void addAttachmentWidgets(QVBoxLayout *layout,
                            const std::vector<DuckClientNoteAttachment> &attachments);
  void refreshPendingAttachments();
  void updateAppointmentSummary(const QVector<DuckEvent> &events);
  void onLinkSessionButtonClicked();
  void updateLinkButtonText();
  void updateJumpButtonsVisibility();
  [[nodiscard]] std::optional<DuckEvent> nearestPastEvent(const QVector<DuckEvent> &events) const;
  [[nodiscard]] QString relativeNoteAttachmentPath(int64_t clientId,
                                                   int64_t noteId,
                                                   const QString &fileName) const;
  bool persistPendingAttachments(int64_t noteId);
  [[nodiscard]] QString currentClientTitle() const;

  std::shared_ptr<pcm::database::Database> mDb;
  CallNoteActions mCallActions;
  QJsonArray mRenderedEntries;
  bool mEntriesRendered = false;
  qint64 mMaterialId = 0;
  QString mEditingEntryId;
  QString mSavedEditorText;
  bool mSavingCallEntry = false;
  bool mManualSaveFailed = false;
  bool mSavingDraftAttachments = false;
  bool mAttachmentSaveFailed = false;
  bool mEmbedded = false;
  struct JournalEntry {
    QString key;
    QString title;
    QString preview;
    qint64 timestamp = 0;
    bool session = false;
    bool hasAgreements = false;
    std::optional<DuckEvent> event;
    std::vector<DuckClientNote> notes;
    QJsonArray calls;
  };
  QString journalEntryMarkdown(const JournalEntry &entry) const;
  std::vector<JournalEntry> mJournalEntries;
  std::function<QString(qint64, const QString &, const QString &)> mMaterialAttachmentResolver;
  bool mJournalMode = false;
  QQuickWidget *mQuickWorkspace = nullptr;
  QWidget *mExternalHeader = nullptr;
  QWidget *mFeedSurface = nullptr;
  QWidget *mJournalTools = nullptr;
  QSplitter *mJournal = nullptr;
  QWidget *mJournalDocument = nullptr;
  QWidget *mJournalActions = nullptr;
  QListWidget *mJournalList = nullptr;
  QLineEdit *mJournalSearch = nullptr;
  QComboBox *mJournalFilter = nullptr;
  QWidget *mHeaderSurface = nullptr;
  QGridLayout *mHeaderLayout = nullptr;
  QGridLayout *mComposerActions = nullptr;
  QTextBrowser *mPreview = nullptr;
  QPushButton *mPreviewButton = nullptr;
  QPushButton *mCancelEditButton = nullptr;
  QWidget *mComposerSurface = nullptr;
  std::optional<DuckClient> mCurrentClient;
  QList<PendingAttachment> mPendingAttachments;
  QList<PendingAttachment> mSavedPendingAttachments;
  FeedFilter mFeedFilter = FeedFilter::All;
  QVector<DuckEvent> mCachedFeedEvents;
  std::optional<DuckEvent> mPendingLinkedEvent;
  bool mLinkManuallySet = false;

  QLabel *mClientNameLabel = nullptr;
  QLabel *mAppointmentSummaryLabel = nullptr;
  QPushButton *mOpenClientCardButton = nullptr;
  oclero::qlementine::SegmentedControl *mFeedFilterControl = nullptr;
  QScrollArea *mScrollArea = nullptr;
  QPushButton *mJumpToLatestButton = nullptr;
  QPushButton *mJumpToTodayButton = nullptr;
  QWidget *mTodayAnchorWidget = nullptr;
  QWidget *mFeedWidget = nullptr;
  QVBoxLayout *mFeedLayout = nullptr;
  QLabel *mEmptyLabel = nullptr;
  QPlainTextEdit *mComposer = nullptr;
  QLabel *mSaveStatusLabel = nullptr;
  QListWidget *mPendingAttachmentsList = nullptr;
  QPushButton *mAttachFilesButton = nullptr;
  QPushButton *mLinkSessionButton = nullptr;
  QPushButton *mAddNoteButton = nullptr;
};
