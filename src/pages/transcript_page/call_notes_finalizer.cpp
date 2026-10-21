#include "call_notes_finalizer.h"

CallNotesFinalizer::CallNotesFinalizer(std::shared_ptr<pcm::database::Database> db, QObject *parent)
    : QObject(parent), mNotes(db), mDb(std::move(db)) {
  connect(&mNotes, &CallNotesController::requestReady, this, &CallNotesFinalizer::requestReady);
  connect(&mNotes, &CallNotesController::generationRequested, this, &CallNotesFinalizer::generationRequested);
  connect(&mNotes, &CallNotesController::cancelRequested, this, [this](const QString &token) {
    emit cancelRequested(mNotes.materialId(), token);
  });
  connect(&mNotes, &CallNotesController::changed, this, [this] {
    if (!mCurrent) return;
    const auto id = mCurrent;
    const auto status = mNotes.finalStatus();
    if (!mNotes.busy() && status != "running" && status != "queued") {
      mCurrent = 0;
      mJobs.removeIf([id](const Job &job) { return job.materialId == id; });
      QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
    }
    emit materialChanged(id);
  });
}
void CallNotesFinalizer::setCallActive(bool active) {
  mCallActive = active;
  if (active && mCurrent) {
    const auto id = mCurrent;
    mCurrent = 0; // cancellation below suspends this job rather than completing it
    mNotes.cancel();
    mNotes.deferFinal();
    emit materialChanged(id);
  }
  if (!active) QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
}
void CallNotesFinalizer::enqueue(qint64 id, qint64 pendingTranscript) {
  if (id <= 0) return;
  for (const auto &job : mJobs) if (job.materialId == id) return;
  CallNotesController stored(mDb);
  stored.openMaterial(id);
  if (stored.materialId() != id) return;
  stored.deferFinal();
  mJobs.append({id, pendingTranscript});
  emit materialChanged(id);
  QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
}
bool CallNotesFinalizer::contains(qint64 id) const {
  for (const auto &job : mJobs) if (job.materialId == id) return true;
  return false;
}
void CallNotesFinalizer::transcriptReady(qint64 id) {
  mReady.insert(id);
  QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
}
void CallNotesFinalizer::cancel(qint64 id) {
  bool found = false;
  for (const auto &job : mJobs) if (job.materialId == id) found = true;
  if (!found) return;
  mJobs.removeIf([id](const Job &job) { return job.materialId == id; });
  if (mCurrent == id) {
    mCurrent = 0; mNotes.cancel();
  } else {
    CallNotesController stored(mDb); stored.openMaterial(id); stored.cancel();
  }
  emit materialChanged(id);
  QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
}
void CallNotesFinalizer::handleResult(const QJsonObject &result) { mNotes.handleResult(result); }
void CallNotesFinalizer::workerFailed() { mNotes.workerFailed(); }
void CallNotesFinalizer::setGenerationEnabled(bool enabled) { mNotes.setGenerationEnabled(enabled); }
void CallNotesFinalizer::pump() {
  if (mCallActive || mCurrent) return;
  for (const auto &job : mJobs) {
    if (job.pendingTranscript > 0 && !mReady.contains(job.pendingTranscript)) continue;
    const auto id = job.materialId;
    mNotes.openMaterial(id);
    if (mNotes.materialId() != id) {
      mJobs.removeIf([id](const Job &entry) { return entry.materialId == id; });
      QTimer::singleShot(0, this, &CallNotesFinalizer::pump);
      return;
    }
    mCurrent = id;
    mNotes.requestFinal();
    return;
  }
}
