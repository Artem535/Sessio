#pragma once
#include "database.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QHash>
#include <memory>

class CallPrompterController final : public QObject {
  Q_OBJECT
public:
  explicit CallPrompterController(std::shared_ptr<pcm::database::Database> db, QObject *parent = nullptr);
  void beginTranscript(qint64 transcriptId);
  void addPhrase(const DuckTranscriptPhrase &phrase);
  void resume();
  void pause();
  void endCall();
  void transcriptDeleted(qint64 transcriptId);
  bool evidenceAvailable(const QString &cardId) const;
  void flush();
  void handleResult(const QJsonObject &result);
  void workerFailed();
  bool acceptCard(const QString &id, const QString &editedText = {});
  bool rejectCard(const QString &id);
  bool resolveCard(const QString &id, const QString &targetId);
  bool saveNote(const QString &id, qint64 clientId);
  QJsonArray cards() const { return mCards; }
  QJsonArray records() const { return mRecords; }
  QJsonArray suggestions() const { return mSuggestions; }
  qint64 transcriptId() const { return mTranscriptId; }
  bool running() const { return mRunning; }
  QString status() const { return mStatus; }
  QJsonObject source(const QString &id) const { return mSources.value(id); }
  std::vector<int64_t> clientIds() const;
signals:
  void requestReady(QJsonObject request);
  void cardsChanged();
  void statusChanged(QString status);
private:
  bool persist();
  void setStatus(const QString &status);
  QJsonObject state() const;
  void restore(const QJsonObject &state);
  int cardIndex(const QString &id) const;
  bool evidenceUnchanged(const QJsonObject &card) const;
  std::shared_ptr<pcm::database::Database> mDb;
  QTimer mBatchTimer;
  qint64 mTranscriptId = 0, mGeneration = 0, mRevision = 0, mRequestId = 0, mLastProcessed = 0;
  bool mRunning = false, mInFlight = false;
  QJsonArray mCards, mRecords, mSuggestions, mPending, mInFlightPhrases, mLookback;
  QHash<QString, QJsonObject> mSources;
  QJsonObject mRequest;
  QString mStatus;
};
