#pragma once
#include "speaker_rename.h"
#include "transcription_session.h"
#include <QHash>
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
  bool focusPhrase(qint64 phraseId);
  void setDelayed(bool delayed);
  void setNotice(const QString &text);
  void setAudioGap(bool interrupted);
  void setError(const QString &userFacingText);
  void clearPhrases();
  [[nodiscard]] int phraseCount() const;
  // Asks for a new name for every phrase of trackRole and emits
  // speakerRenameRequested; labels change only through setSpeakerName.
  void requestSpeakerRename(const QString &trackRole);
  // Relabels shown phrases of trackRole and names its later unnamed phrases.
  void setSpeakerName(const QString &trackRole, const QString &name);
  void setRenamePromptHook(pcm::transcriptionui::SpeakerNamePrompt prompt) { mRenamePrompt = std::move(prompt); }
signals:
  void startRequested();
  void stopRequested();
  void revokeRequested();
  void exportRequested();
  void speakerRenameRequested(QString trackRole, QString name);
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
  QPushButton *mExport;
  QScrollArea *mScroll;
  QVBoxLayout *mRows;
  QHash<QString, QString> mSpeakerNames;
  pcm::transcriptionui::SpeakerNamePrompt mRenamePrompt;
  int mPhraseCount = 0;
  bool mFollowing = true;
};
