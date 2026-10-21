#pragma once

#include "database.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QStringList>
#include <memory>

// Persists manual call notes independently of client records. Model proposals
// remain source-linked drafts; accepting them never replaces manual text.
class CallNotesController final : public QObject {
  Q_OBJECT
public:
  explicit CallNotesController(std::shared_ptr<pcm::database::Database> db, QObject *parent = nullptr,
                               const QString &attachmentRoot = {});
  void beginCall(std::optional<int64_t> eventId = std::nullopt);
  void attachTranscript(qint64 transcriptId);
  void openTranscript(qint64 transcriptId);
  void openMaterial(qint64 materialId);
  void addPhrase(const DuckTranscriptPhrase &phrase);
  void setGenerationEnabled(bool enabled);
  void finishCall();
  void requestFinal();
  void deferFinal();
  void cancel();
  void revoke();
  void transcriptDeleted(qint64 transcriptId);
  void flush();
  void refreshFromStorage();
  void handleResult(const QJsonObject &result);
  void workerFailed();
  bool setManualText(const QString &text);
  QJsonArray noteEntries() const;
  QStringList draftAttachmentPaths() const;
  bool setDraftAttachmentPaths(const QStringList &paths);
  QString saveNoteEntry(const QString &markdown, const QStringList &attachmentPaths = {},
                        const QString &entryId = {});
  bool removeNoteEntry(const QString &entryId);
  QString noteAttachmentPath(const QString &entryId, const QString &attachmentId) const;
  bool acceptDraft(const QString &editedText = {});
  bool dismissDraft();
  bool editDraft(const QString &text);
  bool appendAgreement(const QString &id, const QString &text);
  QString manualText() const;
  QJsonObject draft() const;
  QJsonArray keyPoints() const;
  QJsonObject source(const QString &id) const;
  bool sourcesCurrent() const;
  QString liveStatus() const;
  QString finalStatus() const;
  QString statusText() const;
  qint64 processedThroughMs() const { return mState["processed_ms"].toInteger(); }
  bool ended() const { return mEnded; }
  bool busy() const { return mInFlight; }
  qint64 materialId() const { return mMaterialId; }
  qint64 transcriptId() const { return mTranscriptId; }
signals:
  void changed();
  void requestReady(QJsonObject request);
  void generationRequested();
  void cancelRequested(QString requestToken);
private:
  bool persist();
  bool syncStored(bool force = false);
  void load(qint64 materialId);
  QJsonArray phrases() const;
  void dispatch(bool final);
  std::shared_ptr<pcm::database::Database> mDb;
  QString mAttachmentRoot;
  QTimer mBatch;
  qint64 mMaterialId = 0, mTranscriptId = 0, mRevision = 0, mGeneration = 0, mRequestId = 0;
  bool mEnabled = false, mEnded = false, mInFlight = false, mDirty = false, mFinalPending = false;
  bool mDeferred = false;
  QJsonObject mState, mRequest;
};
