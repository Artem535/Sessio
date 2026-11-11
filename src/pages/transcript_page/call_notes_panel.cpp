#include "call_notes_panel.h"
#include "call_notes_controller.h"
#include "phrase_card_paint.h"
#include "accent_color.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class NoteCard final : public QFrame {
public:
  using QFrame::QFrame;
protected:
  void paintEvent(QPaintEvent *) override { paintPhraseCard(this, false); }
};
QLabel *label(const QString &text, const char *name, QWidget *parent, bool heading = false) {
  auto *result = new QLabel(text, parent);
  result->setObjectName(QString::fromLatin1(name));
  result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  auto font = result->font(); font.setBold(heading); result->setFont(font);
  result->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return result;
}
void clearLayout(QVBoxLayout *layout) {
  while (auto *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
}
void divider(QVBoxLayout *layout, QWidget *parent) {
  auto *line = new QFrame(parent); line->setFrameShape(QFrame::HLine);
  layout->addWidget(line);
}
QString stateText(const QString &state) {
  if (state == "queued") return CallNotesPanel::tr("Queued");
  if (state == "running") return CallNotesPanel::tr("Generating…");
  if (state == "ready") return CallNotesPanel::tr("Ready for review");
  if (state == "error") return CallNotesPanel::tr("Could not generate notes");
  if (state == "cancelled") return CallNotesPanel::tr("Cancelled");
  if (state == "stale") return CallNotesPanel::tr("Sources changed — review required");
  if (state == "interrupted") return CallNotesPanel::tr("Interrupted — retry available");
  if (state == "unavailable") return CallNotesPanel::tr("Model unavailable");
  return CallNotesPanel::tr("Not started");
}
}

CallNotesPanel::CallNotesPanel(CallNotesController *controller, QWidget *parent)
    : QWidget(parent), mController(controller) {
  auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(16, 16, 16, 16); layout->setSpacing(12);
  auto *manualHeader = new QHBoxLayout;
  manualHeader->addWidget(label(tr("My notes"), "callNotesManualTitle", body, true));
  mSaved = label({}, "callNotesSaveStatus", body); manualHeader->addStretch(); manualHeader->addWidget(mSaved);
  layout->addLayout(manualHeader);
  mManual = new QPlainTextEdit(body); mManual->setObjectName("callNotesManual");
  mManual->setAccessibleName(tr("My notes")); mManual->setPlaceholderText(tr("Write your own notes here…"));
  mManual->setMinimumHeight(100); mManual->setMaximumHeight(132); mManual->setTabChangesFocus(true);
  layout->addWidget(mManual); divider(layout, body);
  mTitle = label(tr("Model draft"), "callNotesDraftTitle", body, true); layout->addWidget(mTitle);
  mLiveStatus = label({}, "callNotesLiveStatus", body); layout->addWidget(mLiveStatus);
  mFinalStatus = label({}, "callNotesFinalStatus", body); layout->addWidget(mFinalStatus);
  mEmpty = label(tr("Model notes will appear here after completed phrases. Your own notes are saved separately."), "callNotesEmpty", body);
  layout->addWidget(mEmpty);
  mDraftCard = new NoteCard(body); auto *draftLayout = new QVBoxLayout(mDraftCard);
  draftLayout->setContentsMargins(14, 12, 14, 12); draftLayout->setSpacing(10);
  mDraftText = label({}, "callNotesDraftText", mDraftCard); draftLayout->addWidget(mDraftText);
  mEditor = new QPlainTextEdit(mDraftCard); mEditor->setObjectName("callNotesDraftEditor");
  mEditor->setAccessibleName(tr("Edit model draft")); mEditor->setMinimumHeight(110); mEditor->setTabChangesFocus(true);
  draftLayout->addWidget(mEditor); mEditor->hide();
  mSources = new QVBoxLayout; mSources->setSpacing(4); draftLayout->addLayout(mSources);
  auto *actions = new QHBoxLayout;
  auto button = [](const QString &text, const char *name, QWidget *parent) {
    auto *result = new QPushButton(text, parent); result->setObjectName(name);
    result->setAutoDefault(false); return result;
  };
  mAccept = button(tr("To my notes"), "callNotesAccept", mDraftCard);
  mEdit = button(tr("Edit"), "callNotesEdit", mDraftCard);
  mDismiss = button(tr("Dismiss"), "callNotesDismiss", mDraftCard);
  actions->addWidget(mAccept); actions->addWidget(mEdit); actions->addWidget(mDismiss);
  draftLayout->addLayout(actions); layout->addWidget(mDraftCard);
  mProgress = new QProgressBar(body); mProgress->setRange(0, 0); mProgress->setTextVisible(false);
  mProgress->setFixedHeight(4); layout->addWidget(mProgress);
  auto *generation = new QHBoxLayout;
  mStart = button(tr("Start notes"), "callNotesStart", body);
  mFinal = button(tr("Generate final note"), "callNotesFinal", body);
  mCancel = button(tr("Cancel"), "callNotesCancel", body);
  generation->addWidget(mStart); generation->addWidget(mFinal); generation->addWidget(mCancel);
  layout->addLayout(generation); divider(layout, body);
  auto *pointsHeader = new QHBoxLayout;
  pointsHeader->addWidget(label(tr("Key points"), "callNotesPointsTitle", body, true), 1);
  mPointCount = label({}, "callNotesPointCount", body); pointsHeader->addWidget(mPointCount); layout->addLayout(pointsHeader);
  mPointCount->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  mPoints = new QVBoxLayout; mPoints->setSpacing(8); layout->addLayout(mPoints); layout->addStretch();
  scroll->setWidget(body); outer->addWidget(scroll, 1);
  auto *footer = new NoteCard(this); auto *footerLayout = new QVBoxLayout(footer);
  footerLayout->setContentsMargins(16, 10, 16, 10);
  mStatus = label({}, "callNotesStatus", footer); footerLayout->addWidget(mStatus); outer->addWidget(footer);

  connect(mManual, &QPlainTextEdit::textChanged, this, [this] {
    if (!mController) return;
    mSavingManual = true;
    const bool saved = mController->setManualText(mManual->toPlainText());
    mSavingManual = false; mSaveFailed = !saved;
    mSaved->setText(saved ? tr("Saved") : tr("Could not save — keep this page open"));
    if (saved) emit noteSaved();
  });
  connect(mStart, &QPushButton::clicked, this, &CallNotesPanel::startRequested);
  connect(mFinal, &QPushButton::clicked, this, [this] { if (mController) mController->requestFinal(); });
  connect(mCancel, &QPushButton::clicked, this, [this] { if (mController) mController->cancel(); });
  connect(mAccept, &QPushButton::clicked, this, [this] {
    if (!mController || mController->draft()["accepted"].toBool()) return;
    if (mController->acceptDraft(mEditing ? mEditor->toPlainText() : QString{})) {
      mEditing = false; refresh(); emit noteSaved();
    }
  });
  connect(mEdit, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    if (!mEditing) {
      mEditOrigin = mController->draft(); mEditor->setPlainText(mEditOrigin["text"].toString());
      mEditing = true; refresh(); mEditor->setFocus();
    } else if (mEditOrigin == mController->draft() && mController->editDraft(mEditor->toPlainText())) {
      mEditing = false; refresh();
    }
  });
  connect(mDismiss, &QPushButton::clicked, this, [this] {
    if (mEditing) { mEditing = false; refresh(); return; }
    if (mController && mController->dismissDraft()) { mEditing = false; refresh(); }
  });
  if (controller) {
    connect(controller, &CallNotesController::changed, this, &CallNotesPanel::refresh);
    connect(controller, &QObject::destroyed, this, [this] { setEnabled(false); });
  }
  refresh();
}

void CallNotesPanel::populateSources(QVBoxLayout *layout, const QJsonObject &note) {
  for (const auto &value : note["evidence_ids"].toArray()) {
    const auto id = value.toString(); const auto source = mController->source(id);
    if (source.isEmpty()) continue;
    const auto ms = std::max<qint64>(0, source["start_ms"].toInteger());
    const auto time = QStringLiteral("%1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'));
    auto *jump = new QPushButton(tr("Source · %1").arg(time), this);
    jump->setObjectName("callNotesSource"); jump->setFlat(true); jump->setAutoDefault(false);
    jump->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto colors = jump->palette(); colors.setColor(QPalette::ButtonText, pcm::widgets::accentColor(palette())); jump->setPalette(colors);
    const auto quote = source["speaker"].toString() + QStringLiteral(": ") + source["text"].toString();
    jump->setToolTip(quote); jump->setAccessibleDescription(quote);
    bool validId = false; const auto phraseId = id.toLongLong(&validId);
    jump->setProperty("validPhraseId", validId && phraseId > 0);
    jump->setEnabled(validId && phraseId > 0 && mController->sourcesCurrent());
    connect(jump, &QPushButton::clicked, this, [this, phraseId] { emit sourceRequested(phraseId); });
    layout->addWidget(jump);
  }
}

void CallNotesPanel::refresh() {
  if (!mController) { setEnabled(false); return; }
  const bool changedMaterial = mMaterialId != mController->materialId();
  if (changedMaterial && !mSavingManual) {
    mEditing = false; mSaveFailed = false;
  }
  mMaterialId = mController->materialId();
  mManual->setEnabled(mMaterialId > 0);
  // Repainting generation state must never reset the manual editor's cursor,
  // selection, undo history, or a failed local save.
  if (!mSavingManual && !mSaveFailed && (changedMaterial || !mManual->hasFocus()) && mManual->toPlainText() != mController->manualText()) {
    const QSignalBlocker blocker(mManual); mManual->setPlainText(mController->manualText());
  }
  if (!mSaveFailed && !mSavingManual) mSaved->setText(mController->manualText().isEmpty() ? QString{} : tr("Saved"));
  const auto draft = mController->draft(); const auto points = mController->keyPoints();
  const bool hasDraft = !draft["text"].toString().trimmed().isEmpty();
  const auto live = mController->liveStatus(); const auto final = mController->finalStatus();
  const bool busy = mController->busy() || live == "queued" || final == "queued";
  const bool current = mController->sourcesCurrent();
  const bool conflict = mEditing && draft != mEditOrigin;
  mLiveStatus->setText(tr("Live notes · %1").arg(stateText(hasDraft && !current && !mController->ended() ? QStringLiteral("stale") : live)));
  mFinalStatus->setText(tr("Final note · %1").arg(stateText(final)));
  mFinalStatus->setVisible(mController->ended() || (!final.isEmpty() && final != "idle"));
  mTitle->setText(mController->ended() && final == "ready" ? tr("Final meeting note") : tr("Model draft"));
  mStatus->setText(mMaterialId > 0 ? mController->statusText() : tr("Open a call to view or write notes."));
  if (hasDraft && !current) mStatus->setText(tr("The transcript changed. Generate the note again."));
  if (conflict) mStatus->setText(tr("The model draft changed. Your edit is kept below; cancel editing to review the new draft."));
  mDraftText->setText((mEditing ? mEditOrigin : draft)["text"].toString()); mDraftText->setVisible(!mEditing);
  mEditor->setVisible(mEditing); mDraftCard->setVisible(hasDraft || mEditing); mEmpty->setVisible(!hasDraft && !mEditing);
  QJsonArray sources;
  for (const auto &id : draft["evidence_ids"].toArray()) sources.append(mController->source(id.toString()));
  for (const auto &point : points)
    for (const auto &id : point.toObject()["evidence_ids"].toArray()) sources.append(mController->source(id.toString()));
  // Keep existing buttons alive during unrelated status changes, including while
  // a source link has keyboard focus.
  if (!mEditing && (draft != mRenderedDraft || points != mRenderedPoints || sources != mRenderedSources || property("sourcesCurrent").toBool() != current)) {
    clearLayout(mSources); clearLayout(mPoints); populateSources(mSources, draft);
    for (const auto &value : points) {
      const auto point = value.toObject(); if (point["text"].toString().trimmed().isEmpty()) continue;
      auto *card = new NoteCard(this); auto *row = new QVBoxLayout(card); row->setContentsMargins(12, 10, 12, 10);
      row->addWidget(label(QStringLiteral("• ") + point["text"].toString(), "callNotesPoint", card));
      populateSources(row, point); mPoints->addWidget(card);
    }
    mRenderedDraft = draft; mRenderedPoints = points; mRenderedSources = sources; setProperty("sourcesCurrent", current);
  }
  for (auto *source : findChildren<QPushButton *>("callNotesSource"))
    source->setEnabled(source->property("validPhraseId").toBool() && current && !conflict);
  mPointCount->setText(QString::number(mPoints->count()));
  const bool editable = hasDraft && current && !busy && (!mEditing || draft == mEditOrigin);
  mAccept->setEnabled(editable && !draft["accepted"].toBool());
  mAccept->setText(draft["accepted"].toBool() && hasDraft ? tr("Added to my notes") : tr("To my notes"));
  mEdit->setEnabled(editable); mEdit->setText(mEditing ? tr("Save edit") : tr("Edit"));
  mDismiss->setEnabled(mEditing || (hasDraft && !busy));
  mDismiss->setText(mEditing ? tr("Cancel edit") : tr("Dismiss"));
  mProgress->setVisible(busy); mCancel->setVisible(busy);
  mStart->setVisible(!mController->ended()); mStart->setEnabled(!busy);
  mStart->setText(live == "error" || live == "cancelled" || live == "interrupted" || live == "stale" ? tr("Retry notes") : tr("Start notes"));
  mFinal->setVisible(mController->ended()); mFinal->setEnabled(!busy && mController->transcriptId() > 0);
  mFinal->setText(final == "error" || final == "cancelled" || final == "interrupted" || final == "stale" ? tr("Retry final note") : tr("Generate final note"));
}
