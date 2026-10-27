#pragma once
#include "database.h"
#include <QWidget>
#include <functional>
#include <QVariantList>

class QLabel;
class QVBoxLayout;
class QWidget;
class TranscriptListPage final : public QWidget {
  Q_OBJECT
  Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
  Q_PROPERTY(QString notice READ notice NOTIFY recordsChanged)
public:
  explicit TranscriptListPage(std::shared_ptr<pcm::database::Database> db, QWidget *parent = nullptr);
  void reload();
  void setConfirmHook(std::function<bool(const QString &)> hook);
  [[nodiscard]] int transcriptCount() const { return mCount; }
  void enableQuickWorkspace();
  QWidget *headerControls();
  QVariantList records() const;
  QString notice() const;
  Q_INVOKABLE void openRecord(const QString &id, bool material);
  Q_INVOKABLE void removeRecord(const QString &id);
signals:
  void recordsChanged();
  void openTranscriptRequested(qint64 transcriptId);
  void transcriptsChanged();
  void openCallMaterialRequested(qint64 materialId);
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
  class QQuickWidget *mQuick = nullptr;
  QWidget *mExternalHeader = nullptr;
};
