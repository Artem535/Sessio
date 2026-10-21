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
class QGridLayout;

class CallNotesPanel final : public QWidget {
  Q_OBJECT
public:
  explicit CallNotesPanel(CallNotesController *controller, QWidget *parent = nullptr);
  // Inline AI card embedded in ClientNotesPage. Acceptance appends to the
  // controller's manual draft; only the page's explicit Save creates an entry.
  void setInlineMode(bool enabled);
signals:
  void sourceRequested(qint64 phraseId);
  void startRequested();
  void generationCancelled();
  void noteSaved();
  void editorRequested();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
  void changeEvent(QEvent *event) override;
private:
  void refresh();
  void arrangeActions();
  void updatePresentationColors();
  void populateSources(QVBoxLayout *layout, const QJsonObject &note, bool compact = false);
  QPointer<CallNotesController> mController;
  QPlainTextEdit *mManual;
  QPlainTextEdit *mEditor;
  QLabel *mSaved;
  QLabel *mTitle;
  QLabel *mCoverage;
  QLabel *mDraftText;
  QLabel *mEmpty;
  QLabel *mLiveStatus;
  QLabel *mFinalStatus;
  QLabel *mStatus;
  QLabel *mPointCount;
  QLabel *mPointsTitle = nullptr;
  QWidget *mDraftCard;
  QWidget *mViewport;
  QVBoxLayout *mSources;
  QVBoxLayout *mPoints;
  QGridLayout *mActions;
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
  bool mNarrowActions = false;
  bool mInlineMode = false;
  QWidget *mInlineBody = nullptr;
  QPushButton *mCollapse = nullptr;
  QPushButton *mUsePoints = nullptr;
};
