#pragma once

#include "call_notes_controller.h"
#include <QList>
#include <QSet>

// Call-owned final jobs outlive the panel's current call. They yield the shared
// model to a new live call and resume only after its transcription has drained.
class CallNotesFinalizer final : public QObject {
  Q_OBJECT
public:
  explicit CallNotesFinalizer(std::shared_ptr<pcm::database::Database> db, QObject *parent = nullptr);
  void setCallActive(bool active);
  void enqueue(qint64 materialId, qint64 pendingTranscript = 0);
  void transcriptReady(qint64 transcriptId);
  void cancel(qint64 materialId);
  bool contains(qint64 materialId) const;
  void handleResult(const QJsonObject &result);
  void workerFailed();
  void setGenerationEnabled(bool enabled);
signals:
  void requestReady(QJsonObject request);
  void generationRequested();
  void cancelRequested(qint64 materialId, QString token);
  void materialChanged(qint64 materialId);
private:
  struct Job { qint64 materialId; qint64 pendingTranscript; };
  void pump();
  CallNotesController mNotes;
  std::shared_ptr<pcm::database::Database> mDb;
  QList<Job> mJobs;
  QSet<qint64> mReady;
  bool mCallActive = false;
  qint64 mCurrent = 0;
};
