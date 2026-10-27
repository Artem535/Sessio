#pragma once
#include "database.h"
#include <QWidget>
#include <functional>
#include <memory>
#include <QVariantList>
#include <QVariantMap>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QSplitter;
class QLineEdit;

class TranscriptPage final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(QVariantMap reader READ reader NOTIFY readerChanged)
  Q_PROPERTY(QVariantList readerHistory READ readerHistory NOTIFY readerChanged)
  Q_PROPERTY(QVariantList readerPhrases READ readerPhrases NOTIFY readerChanged)
public:
  TranscriptPage(std::shared_ptr<pcm::database::Database> db, int64_t eventId,
                 const QString &title, QWidget *parent = nullptr);
  ~TranscriptPage() override;
  void reload(int64_t eventId, const QString &title);
  void reloadTranscript(int64_t transcriptId, const QString &title);
  QString exportText() const;
  [[nodiscard]] int transcriptCount() const;
  [[nodiscard]] bool editing() const { return mEditing; }
  void setConfirmHook(std::function<bool(const QString &)> hook);
  // Main document of the notes/transcript workspace. The page reparents the view
  // into its own layout but does not own it beyond Qt's parent-child cleanup;
  // callers typically keep a QPointer to it. The «Call notes» button becomes a
  // visibility toggle once a view is attached. Transcript sources open on the right.
  void setNotesView(QWidget *view);
  [[nodiscard]] bool notesVisible() const { return mNotesVisible; }
  void setNotesVisible(bool visible);
  bool focusPhrase(qint64 phraseId);
  void enableQuickReader();
  QWidget *headerControls();
  QVariantMap reader() const;
  QVariantList readerHistory() const;
  QVariantList readerPhrases() const;
  Q_INVOKABLE void selectReaderTranscript(const QString &id);
  Q_INVOKABLE void readerAction(const QString &action);
  Q_INVOKABLE void setReaderMode(bool reading);
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
signals:
  void readerChanged();
  void readerPhraseFocused(QString id);
  void readerTranscriptSelected();
  void backRequested();
  void transcriptsChanged();
  // Emitted when the transcript currently rendered changes (not on every
  // re-render, so an in-progress note edit is never clobbered).
  void transcriptActivated(qint64 transcriptId);
private:
  void showTranscript();
  bool mayMutate(int64_t transcriptId);
  void reportFailure();
  void filterPhrases();
  void activateTranscript(qint64 id);
  void updatePhraseSizes();
  bool mEditing = false;
  class QQuickWidget *mQuickReader = nullptr;
  QWidget *mExternalHeader = nullptr;
  std::shared_ptr<pcm::database::Database> mDb;
  int64_t mEventId = 0;
  std::optional<int64_t> mTranscriptId;
  QComboBox *mSelector;
  QLabel *mTitle;
  QLabel *mClient;
  QLabel *mMetadata;
  QLabel *mStatus;
  QLabel *mNotice;
  QLineEdit *mSearch;
  QPushButton *mReview;
  QPushButton *mDelete;
  QPushButton *mAttach;
  QPushButton *mNotesToggle;
  QSplitter *mSplitter;
  QWidget *mNotesHost;
  QWidget *mTranscriptHost;
  QPushButton *mSourceToggle;
  QWidget *mNotesView = nullptr;
  bool mNotesVisible = false;
  bool mSplitInitialized = false;
  qint64 mActivatedTranscriptId = 0;
  QVBoxLayout *mRows;
  std::function<bool(const QString &)> mConfirm;
};
