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

namespace oclero::qlementine {
class SegmentedControl;
}

class ClientNotesPage final : public QWidget {
  Q_OBJECT

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
  ~ClientNotesPage() override = default;
  void setCallNoteActions(qint64 materialId, CallNoteActions actions);
  void setAiPanelWidget(QWidget *panel);
  // Shows only this meeting's composer and entries; an outer scroll area scrolls the page.
  void setEmbedded(bool embedded);
  void focusComposer();
  void addPendingAttachments(const QStringList &paths);

signals:
  void openClientCardRequested(const std::optional<DuckClient> &client);
  void openEventRequested(int64_t eventId, qint64 dayMs);
  void noteEntrySaved(QString entryId);

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
  QPointer<QWidget> mAiPanel;
  QWidget *mAiHost = nullptr;
  bool mEmbedded = false;
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
