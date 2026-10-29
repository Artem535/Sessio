#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class CallNotesController;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

class CallNotesPanel final : public QWidget {
  Q_OBJECT
public:
  explicit CallNotesPanel(CallNotesController *controller, QWidget *parent = nullptr);
signals:
  void sourceRequested(qint64 phraseId);
  void startRequested();
  void noteSaved();
private:
  void refresh();
  void populateSources(QVBoxLayout *layout, const QJsonObject &note);
  QPointer<CallNotesController> mController;
  QPlainTextEdit *mManual;
  QPlainTextEdit *mEditor;
  QLabel *mSaved;
  QLabel *mTitle;
  QLabel *mDraftText;
  QLabel *mEmpty;
  QLabel *mLiveStatus;
  QLabel *mFinalStatus;
  QLabel *mStatus;
  QLabel *mPointCount;
  QWidget *mDraftCard;
  QVBoxLayout *mSources;
  QVBoxLayout *mPoints;
  QPushButton *mAccept;
  QPushButton *mEdit;
  QPushButton *mDismiss;
  QPushButton *mStart;
  QPushButton *mFinal;
  QPushButton *mCancel;
  QProgressBar *mProgress;
  QJsonObject mRenderedDraft;
  QJsonArray mRenderedPoints;
  QJsonArray mRenderedSources;
  QJsonObject mEditOrigin;
  qint64 mMaterialId = 0;
  bool mEditing = false;
  bool mSavingManual = false;
  bool mSaveFailed = false;
};
