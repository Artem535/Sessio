#pragma once
#include "database.h"
#include <QWidget>
#include <functional>

class QTableWidget;
class QLabel;
class TranscriptListPage final : public QWidget {
  Q_OBJECT
public:
  explicit TranscriptListPage(std::shared_ptr<pcm::database::Database> db, QWidget *parent = nullptr);
  void reload();
  void setConfirmHook(std::function<bool(const QString &)> hook);
signals:
  void openTranscriptRequested(qint64 transcriptId);
  void transcriptsChanged();
protected:
  void showEvent(QShowEvent *event) override;
private:
  std::shared_ptr<pcm::database::Database> mDb;
  QTableWidget *mTable;
  QLabel *mNotice;
  std::function<bool(const QString &)> mConfirm;
};
