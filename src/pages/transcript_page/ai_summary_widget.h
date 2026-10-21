#pragma once

#include "notes_widget_frame.h"
#include <QJsonObject>
#include <QPointer>
#include <functional>

class CallNotesController;
class QPlainTextEdit;

// «AI summary» card over the call notes controller.
class AiSummaryWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit AiSummaryWidget(CallNotesController *controller, QWidget *parent = nullptr);
  // Replaces the delete confirmation dialog (tests). Returns true to delete.
  void setDeleteConfirmation(std::function<bool(QWidget *)> confirm);
  // False when there is nothing to show: no summary and an idle status.
  bool hasContent() const;

signals:
  void sourceRequested(qint64 phraseId);
  void retryRequested();  // during the call: host restarts live notes
  void contentChanged();  // visibility may need updating

private:
  void refresh();
  void retry();
  QPointer<CallNotesController> mController;
  std::function<bool(QWidget *)> mConfirm;
  QLabel *mText;
  QLabel *mCoverage;
  QLabel *mProblem;
  QPlainTextEdit *mEditor;
  QVBoxLayout *mSources;
  QPushButton *mEdit;
  QPushButton *mSave;
  QPushButton *mCancel;
  QPushButton *mRetry;
  QJsonObject mRenderedDraft;
  bool mEditing = false;
  bool mRenderedCurrent = true;
  qint64 mMaterialId = 0;
};
