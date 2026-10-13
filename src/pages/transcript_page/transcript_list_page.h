#pragma once
#include "database.h"
#include <QWidget>
#include <functional>

class QLabel;
class QVBoxLayout;
class QWidget;
class TranscriptListPage final : public QWidget {
  Q_OBJECT
public:
  explicit TranscriptListPage(std::shared_ptr<pcm::database::Database> db, QWidget *parent = nullptr);
  void reload();
  void setConfirmHook(std::function<bool(const QString &)> hook);
  [[nodiscard]] int transcriptCount() const { return mCount; }
signals:
  void openTranscriptRequested(qint64 transcriptId);
  void transcriptsChanged();
protected:
  void showEvent(QShowEvent *event) override;
private:
  void addCard(const DuckTranscript &row);
  void deleteTranscript(qint64 id);
  std::shared_ptr<pcm::database::Database> mDb;
  QLabel *mSummary;
  QLabel *mNotice;
  QLabel *mEmpty;
  QWidget *mList;
  QVBoxLayout *mRows;
  int mCount = 0;
  std::function<bool(const QString &)> mConfirm;
};
