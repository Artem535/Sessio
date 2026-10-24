#include "ai_summary_widget.h"
#include "call_notes_controller.h"
#include "call_notes_sources.h"
#include <QAction>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *textLabel(const char *name, QWidget *parent) {
  auto *label = new QLabel(parent); label->setObjectName(QString::fromLatin1(name));
  label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
}

AiSummaryWidget::AiSummaryWidget(CallNotesController *controller, QWidget *parent)
    : NotesWidgetFrame(tr("AI summary"), parent), mController(controller) {
  mConfirm = [](QWidget *owner) {
    return QMessageBox::question(owner, tr("Delete AI summary"),
        tr("Delete the AI summary of this meeting? Your own notes are kept.")) == QMessageBox::Yes;
  };
  mText = textLabel("aiSummaryText", this); body()->addWidget(mText);
  mEditor = new QPlainTextEdit(this); mEditor->setObjectName("aiSummaryEditor");
  mEditor->setAccessibleName(tr("Edit AI summary")); mEditor->setMinimumHeight(110);
  mEditor->setTabChangesFocus(true); mEditor->hide(); body()->addWidget(mEditor);
  mCoverage = textLabel("aiSummaryCoverage", this); mCoverage->setForegroundRole(QPalette::PlaceholderText);
  body()->addWidget(mCoverage);
  mSources = new QVBoxLayout; mSources->setSpacing(2); body()->addLayout(mSources);
  mProblem = textLabel("aiSummaryProblem", this); body()->addWidget(mProblem);
  mEdit = addAction(tr("Edit"), "aiSummaryEdit");
  mSave = addAction(tr("Save"), "aiSummarySave");
  mCancel = addAction(tr("Cancel"), "aiSummaryCancel");
  mRetry = addAction(tr("Retry"), "aiSummaryRetry");
  auto *regenerate = addMenuAction(tr("Create again"), "aiSummaryRegenerate");
  auto *remove = addMenuAction(tr("Delete"), "aiSummaryDelete");
  connect(mEdit, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    mEditor->setPlainText(mController->draft()["text"].toString());
    mEditing = true; refresh(); mEditor->setFocus();
  });
  connect(mSave, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    if (mController->editDraft(mEditor->toPlainText())) { mEditing = false; refresh(); return; }
    mProblem->setText(tr("Could not save — keep this page open")); mProblem->show();
  });
  connect(mCancel, &QPushButton::clicked, this, [this] { mEditing = false; refresh(); });
  connect(mRetry, &QPushButton::clicked, this, &AiSummaryWidget::retry);
  connect(regenerate, &QAction::triggered, this, &AiSummaryWidget::retry);
  connect(remove, &QAction::triggered, this, [this] {
    if (mController && mConfirm(this) && mController->dismissDraft()) { mEditing = false; refresh(); }
  });
  if (controller) connect(controller, &CallNotesController::changed, this, &AiSummaryWidget::refresh);
  refresh();
}

void AiSummaryWidget::setDeleteConfirmation(std::function<bool(QWidget *)> confirm) { mConfirm = std::move(confirm); }

void AiSummaryWidget::retry() {
  if (!mController) return;
  if (mController->ended()) mController->requestFinal(); else emit retryRequested();
}

bool AiSummaryWidget::hasContent() const {
  if (!mController || mController->materialId() <= 0) return false;
  const auto state = mController->ended() ? mController->finalStatus() : mController->liveStatus();
  return !mController->draft()["text"].toString().trimmed().isEmpty() || (state != "idle" && state != "ready");
}

void AiSummaryWidget::refresh() {
  if (!mController) { setEnabled(false); return; }
  const auto draft = mController->draft();
  if (mController->materialId() != mMaterialId) { mMaterialId = mController->materialId(); mEditing = false; }
  if (draft["text"].toString().trimmed().isEmpty()) mEditing = false;
  const bool hasText = !draft["text"].toString().trimmed().isEmpty();
  const auto state = mController->ended() ? mController->finalStatus() : mController->liveStatus();
  const bool busy = mController->busy() || state == "running" || state == "queued";
  setStatus(busy ? tr("updating…") : state == "ready" && hasText ? tr("ready")
            : state == "stale" ? tr("outdated") : state == "error" ? tr("failed") : QString{});
  mText->setText(draft["text"].toString()); mText->setVisible(hasText && !mEditing);
  mEditor->setVisible(mEditing);
  mCoverage->setText(summaryCoverage(mController)); mCoverage->setVisible(hasText && !mEditing);
  const bool current = mController->sourcesCurrent();
  if (draft != mRenderedDraft || current != mRenderedCurrent) {
    while (auto *item = mSources->takeAt(0)) { delete item->widget(); delete item; }
    if (auto *link = makeSourceLink(mController, draft, false, this, [this](qint64 id) { emit sourceRequested(id); }))
      mSources->addWidget(link);
    mRenderedDraft = draft; mRenderedCurrent = current;
  }
  QString problem;
  bool canRetry = false;
  if (state == "error" || state == "cancelled" || state == "interrupted") {
    problem = mController->statusText(); canRetry = true; mRetry->setText(tr("Retry"));
  } else if (state == "stale") {
    problem = tr("The transcript changed after this summary."); canRetry = true; mRetry->setText(tr("Update"));
  } else if (state == "unavailable") {
    problem = tr("Turn on transcription to get a summary");
  }
  mProblem->setText(problem); mProblem->setVisible(!problem.isEmpty());
  mRetry->setVisible(canRetry && !mEditing); mRetry->setEnabled(!busy);
  const bool editable = hasText && mController->sourcesCurrent() && !busy;
  mEdit->setVisible(!mEditing && hasText); mEdit->setEnabled(editable);
  mSave->setVisible(mEditing); mCancel->setVisible(mEditing);
  if (auto *more = findChild<QWidget *>("notesWidgetMore")) more->setVisible(hasText && !mEditing && !busy);
  emit contentChanged();
}
