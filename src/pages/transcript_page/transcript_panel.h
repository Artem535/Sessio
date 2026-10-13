#pragma once
#include "transcription_session.h"
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class TranscriptPanel final : public QWidget {
  Q_OBJECT
public:
  explicit TranscriptPanel(QWidget *parent = nullptr);
  void setState(pcm::calltranscription::SessionState state);
  void setStartAvailable(bool available, const QString &reason);
  void addPhrase(const DuckTranscriptPhrase &phrase);
  void setDelayed(bool delayed);
  void setNotice(const QString &text);
  void setAudioGap(bool interrupted);
  void setError(const QString &userFacingText);
  void clearPhrases();
  [[nodiscard]] int phraseCount() const;
signals:
  void startRequested();
  void stopRequested();
  void revokeRequested();
private:
  QLabel *mStatus;
  QLabel *mReason;
  QLabel *mDelayed;
  QLabel *mNotice;
  QLabel *mAudioGap;
  QLabel *mError;
  QLabel *mListening;
  QPushButton *mStart;
  QPushButton *mStop;
  QPushButton *mRevoke;
  QScrollArea *mScroll;
  QVBoxLayout *mRows;
  int mPhraseCount = 0;
  bool mFollowing = true;
};
