#pragma once
#include "transcription_tuning.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QLineEdit;
class TranscriptionSettingsPanel final : public QWidget {
  Q_OBJECT
public:
  explicit TranscriptionSettingsPanel(QWidget *parent = nullptr);
  void setEnabledState(bool enabled);
  void setAutoStartState(bool transcription, bool prompter);
  void setPrompterModel(const QString &model);
  void setPrompterAcceleration(const QString &mode);
  void setModelInfo(const QString &modelId, bool installed);
  void setTranscriptCount(int count);
  void setDeleteAllAllowed(bool allowed, const QString &reason);
  void setTuning(const pcm::transcription::TranscriptionTuning &tuning);
  [[nodiscard]] pcm::transcription::TranscriptionTuning tuning() const;
signals:
  void enabledToggled(bool enabled);
  void autoStartToggled(bool enabled);
  void prompterAutoStartToggled(bool enabled);
  void prompterModelChanged(QString model);
  void prompterAccelerationChanged(QString mode);
  void deleteAllRequested();
  // Emitted when the user edits a speech parameter or resets them to the defaults.
  void tuningChanged(const pcm::transcription::TranscriptionTuning &tuning);
private:
  QCheckBox *mEnabled;
  QCheckBox *mAutoStart;
  QCheckBox *mPrompterAutoStart;
  QLineEdit *mPrompterModel;
  QComboBox *mPrompterAcceleration;
  QLabel *mModel;
  QLabel *mCount;
  QPushButton *mDelete;
  QDoubleSpinBox *mThreshold;
  QDoubleSpinBox *mMinSilence;
  QDoubleSpinBox *mMinSpeech;
  QDoubleSpinBox *mMaxPhrase;
  QSpinBox *mThreads;
  int mTranscriptCount = 0;
  bool mDeleteAllowed = true;
};
