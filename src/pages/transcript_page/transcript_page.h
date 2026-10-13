#pragma once
#include "database.h"
#include <QWidget>
#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

class TranscriptPage final : public QWidget {
  Q_OBJECT
public:
  TranscriptPage(std::shared_ptr<pcm::database::Database> db, int64_t eventId,
                 const QString &title, QWidget *parent = nullptr);
  void reload(int64_t eventId, const QString &title);
  void reloadTranscript(int64_t transcriptId, const QString &title);
  QString exportText() const;
  [[nodiscard]] int transcriptCount() const;
  [[nodiscard]] bool editing() const { return mEditing; }
  void setConfirmHook(std::function<bool(const QString &)> hook);
signals:
  void backRequested();
  void transcriptsChanged();
private:
  void showTranscript();
  bool mayMutate(int64_t transcriptId);
  void reportFailure();
  bool mEditing = false;
  std::shared_ptr<pcm::database::Database> mDb;
  int64_t mEventId = 0;
  std::optional<int64_t> mTranscriptId;
  QComboBox *mSelector;
  QLabel *mTitle;
  QLabel *mClient;
  QLabel *mMetadata;
  QLabel *mStatus;
  QLabel *mNotice;
  QPushButton *mReview;
  QPushButton *mDelete;
  QPushButton *mAttach;
  QVBoxLayout *mRows;
  std::function<bool(const QString &)> mConfirm;
};
