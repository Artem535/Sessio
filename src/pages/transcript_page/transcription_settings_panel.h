#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class TranscriptionSettingsPanel final : public QWidget {
  Q_OBJECT
public:
  explicit TranscriptionSettingsPanel(QWidget *parent = nullptr);
  void setEnabledState(bool enabled);
  void setModelInfo(const QString &modelId, bool installed);
  void setTranscriptCount(int count);
  void setDeleteAllAllowed(bool allowed, const QString &reason);
signals:
  void enabledToggled(bool enabled);
  void deleteAllRequested();
private:
  QCheckBox *mEnabled;
  QLabel *mModel;
  QLabel *mCount;
  QPushButton *mDelete;
  int mTranscriptCount = 0;
  bool mDeleteAllowed = true;
};
