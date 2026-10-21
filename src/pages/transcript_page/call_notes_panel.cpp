#include "call_notes_panel.h"
#include "call_notes_controller.h"
#include "call_notes_sources.h"
#include "accent_color.h"
#include <QFrame>
#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <oclero/qlementine/style/QlementineStyle.hpp>

namespace {
QColor noteForeground(bool muted = false, bool disabled = false) {
  if (auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style())) {
    using namespace oclero::qlementine;
    auto color = style->colorForTextRole(TextRole::Default,
                                       disabled ? MouseState::Disabled : MouseState::Normal);
    if (muted && !disabled) {
      const auto background = QApplication::palette().color(QPalette::Window);
      color.setRgbF(color.redF() * 0.72 + background.redF() * 0.28,
                    color.greenF() * 0.72 + background.greenF() * 0.28,
                    color.blueF() * 0.72 + background.blueF() * 0.28);
    }
    return color;
  }
  auto color = QApplication::palette().color(disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
  if (muted) color.setAlpha(175);
  return color;
}
QColor noteAccent() {
  if (auto *style = dynamic_cast<oclero::qlementine::QlementineStyle *>(QApplication::style()))
    return style->theme().primaryColor;
  return pcm::widgets::accentColor();
}
enum class NoteGlyph { Check, Pencil, Close, Quote };

class NoteIconEngine final : public QIconEngine {
public:
  explicit NoteIconEngine(NoteGlyph glyph) : mGlyph(glyph) {}
  QIconEngine *clone() const override { return new NoteIconEngine(mGlyph); }
  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
    const auto color = mode == QIcon::Disabled ? noteForeground(false, true) :
        mGlyph == NoteGlyph::Quote ? noteAccent() : noteForeground();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);
    painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (mGlyph) {
    case NoteGlyph::Check:
      path.moveTo(5, 12); path.lineTo(10, 17); path.lineTo(19, 7);
      break;
    case NoteGlyph::Pencil:
      path.moveTo(4, 16); path.lineTo(4, 20); path.lineTo(8, 20);
      path.lineTo(20, 8); path.lineTo(16, 4); path.closeSubpath();
      path.moveTo(13, 7); path.lineTo(17, 11);
      break;
    case NoteGlyph::Close:
      path.moveTo(7, 7); path.lineTo(17, 17);
      path.moveTo(17, 7); path.lineTo(7, 17);
      break;
    case NoteGlyph::Quote:
      for (const int x : {4, 14}) {
        path.addRoundedRect(QRectF(x, 6, 6, 7), 1, 1);
        path.moveTo(x + 6, 13); path.cubicTo(x + 6, 16, x + 3, 18, x, 18);
      }
      break;
    }
    painter->drawPath(path);
    painter->restore();
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    QPixmap result(size); result.fill(Qt::transparent);
    QPainter painter(&result); paint(&painter, QRect(QPoint{}, size), mode, state);
    return result;
  }
private:
  NoteGlyph mGlyph;
};

QIcon noteIcon(NoteGlyph glyph) {
  return QIcon(new NoteIconEngine(glyph));
}

class DraftExcerpt final : public QFrame {
public:
  using QFrame::QFrame;
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setPen(QPen(pcm::widgets::accentColor(palette()), 2));
    painter.drawLine(1, 0, 1, height());
  }
};
class NoteFooter final : public QFrame {
public:
  using QFrame::QFrame;
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window).darker(105));
    auto line = palette().color(QPalette::Mid); line.setAlpha(100);
    painter.setPen(line); painter.drawLine(0, 0, width(), 0);
  }
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
}

CallNotesPanel::CallNotesPanel(CallNotesController *controller, QWidget *parent)
    : QWidget(parent), mController(controller) {
  auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  mViewport = scroll->viewport();
  scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(16, 16, 16, 16); layout->setSpacing(12);
  auto *manualHeader = new QHBoxLayout;
  auto *manualTitle = label(tr("My notes").toUpper(), "callNotesManualTitle", body, true);
  auto titleFont = manualTitle->font(); titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.7);
  manualTitle->setFont(titleFont); manualTitle->setForegroundRole(QPalette::PlaceholderText);
  manualHeader->addWidget(manualTitle);
  mSaved = label({}, "callNotesSaveStatus", body); manualHeader->addStretch(); manualHeader->addWidget(mSaved);
  mSaved->setForegroundRole(QPalette::PlaceholderText);
  layout->addLayout(manualHeader);
  mManual = new QPlainTextEdit(body); mManual->setObjectName("callNotesManual");
  mManual->setAccessibleName(tr("My notes")); mManual->setPlaceholderText(tr("Write your own notes here…"));
  mManual->setFixedHeight(std::max(82, mManual->fontMetrics().lineSpacing() * 4 + 16));
  mManual->setTabChangesFocus(true);
  layout->addWidget(mManual); divider(layout, body);
  auto *draftHeader = new QHBoxLayout;
  mTitle = label(tr("Model draft"), "callNotesDraftTitle", body, true); draftHeader->addWidget(mTitle, 1);
  mCoverage = label({}, "callNotesCoverage", body); mCoverage->setForegroundRole(QPalette::PlaceholderText);
  draftHeader->addWidget(mCoverage); layout->addLayout(draftHeader);
  mLiveStatus = label({}, "callNotesLiveStatus", body); layout->addWidget(mLiveStatus);
  mFinalStatus = label({}, "callNotesFinalStatus", body); layout->addWidget(mFinalStatus);
  mEmpty = label(tr("Model notes will appear here after completed phrases. Your own notes are saved separately."), "callNotesEmpty", body);
  layout->addWidget(mEmpty);
  mDraftCard = new QWidget(body); auto *draftLayout = new QVBoxLayout(mDraftCard);
  draftLayout->setContentsMargins(0, 0, 0, 0); draftLayout->setSpacing(10);
  auto *excerpt = new DraftExcerpt(mDraftCard); auto *excerptLayout = new QVBoxLayout(excerpt);
  excerptLayout->setContentsMargins(14, 0, 0, 0); excerptLayout->setSpacing(8);
  mDraftText = label({}, "callNotesDraftText", excerpt); excerptLayout->addWidget(mDraftText);
  mEditor = new QPlainTextEdit(excerpt); mEditor->setObjectName("callNotesDraftEditor");
  mEditor->setAccessibleName(tr("Edit model draft")); mEditor->setMinimumHeight(110); mEditor->setTabChangesFocus(true);
  excerptLayout->addWidget(mEditor); mEditor->hide();
  mSources = new QVBoxLayout; mSources->setSpacing(4); excerptLayout->addLayout(mSources);
  draftLayout->addWidget(excerpt);
  mActions = new QGridLayout;
  auto button = [](const QString &text, const char *name, QWidget *parent) {
    auto *result = new QPushButton(text, parent); result->setObjectName(name);
    result->setAutoDefault(false); return result;
  };
  mAccept = button(tr("To my notes"), "callNotesAccept", mDraftCard);
  mAccept->setDefault(true);
  mEdit = button(tr("Edit"), "callNotesEdit", mDraftCard);
  mDismiss = button(tr("Dismiss"), "callNotesDismiss", mDraftCard);
  mEdit->setIcon(noteIcon(NoteGlyph::Pencil));
  mDismiss->setIcon(noteIcon(NoteGlyph::Close));
  const auto outlineStyle = QStringLiteral("QPushButton { background: transparent; color: palette(window-text); border: 1px solid palette(mid); border-radius: 6px; padding: 4px 8px; } QPushButton:disabled { color: palette(mid); }");
  mEdit->setStyleSheet(outlineStyle); mDismiss->setStyleSheet(outlineStyle);
  mDismiss->setFixedSize(30, 28);
  for (auto *action : {mEdit, mDismiss}) action->setIconSize(QSize(16, 16));
  mActions->addWidget(mAccept, 0, 0); mActions->addWidget(mEdit, 0, 1); mActions->addWidget(mDismiss, 0, 2);
  mActions->setColumnStretch(3, 1);
  draftLayout->addLayout(mActions); layout->addWidget(mDraftCard);
  mDraftCard->installEventFilter(this);
  mViewport->installEventFilter(this);
  mProgress = new QProgressBar(body); mProgress->setRange(0, 0); mProgress->setTextVisible(false);
  mProgress->setFixedHeight(4); layout->addWidget(mProgress);
  auto *generation = new QHBoxLayout;
  mStart = button(tr("Start notes"), "callNotesStart", body);
  mFinal = button(tr("Generate final note"), "callNotesFinal", body);
  mCancel = button(tr("Cancel"), "callNotesCancel", body);
  generation->addWidget(mStart); generation->addWidget(mFinal); generation->addWidget(mCancel);
  layout->addLayout(generation); divider(layout, body);
  auto *pointsHeader = new QHBoxLayout;
  mPointsTitle = label(tr("Key points"), "callNotesPointsTitle", body, true);
  pointsHeader->addWidget(mPointsTitle, 1);
  mPointCount = label({}, "callNotesPointCount", body); pointsHeader->addWidget(mPointCount); layout->addLayout(pointsHeader);
  mPointCount->setForegroundRole(QPalette::PlaceholderText);
  mPointCount->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  mPoints = new QVBoxLayout; mPoints->setSpacing(8); layout->addLayout(mPoints); layout->addStretch();
  scroll->setWidget(body); outer->addWidget(scroll, 1);
  mInlineBody = body;
  mCollapse = new QPushButton(tr("AI draft and key points"), this);
  mCollapse->setObjectName("callNotesCollapse"); mCollapse->setCheckable(true);
  mCollapse->setChecked(true); mCollapse->hide();
  outer->insertWidget(0, mCollapse);
  connect(mCollapse, &QPushButton::toggled, this, [this](bool expanded) {
    mInlineBody->setVisible(expanded);
    mCollapse->setProperty("expanded", expanded);
  });
  mUsePoints = button(tr("Add to editor"), "callNotesUsePoints", body);
  layout->insertWidget(layout->count() - 1, mUsePoints);
  mUsePoints->hide();
  connect(mUsePoints, &QPushButton::clicked, this, [this] {
    if (!mController || !mController->sourcesCurrent()) return;
    QStringList points;
    for (const auto &value : mController->keyPoints()) {
      const auto text = value.toObject()["text"].toString();
      if (!text.trimmed().isEmpty()) points.append(QStringLiteral("- ") + text);
    }
    if (points.isEmpty()) return;
    const auto manual = mController->manualText();
    if (mController->setManualText(manual + (manual.isEmpty() ? QString{} : QStringLiteral("\n\n")) + points.join('\n')))
      emit editorRequested();
  });
  auto *footer = new NoteFooter(this); auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(16, 10, 16, 10);
  auto *footerIcon = new QLabel(footer); footerIcon->setObjectName("callNotesStatusIcon");
  footerIcon->setFixedSize(16, 16); footerLayout->addWidget(footerIcon);
  mStatus = label({}, "callNotesStatus", footer); footerLayout->addWidget(mStatus, 1); outer->addWidget(footer);

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
  connect(mCancel, &QPushButton::clicked, this, [this] {
    emit generationCancelled();
    if (mController) mController->cancel();
  });
  connect(mAccept, &QPushButton::clicked, this, [this] {
    return;  // Removed with CallNotesPanel in Task 10.
  });
  connect(mEdit, &QPushButton::clicked, this, [this] {
    if (!mController) return;
    if (mInlineMode) { mAccept->click(); return; }
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

void CallNotesPanel::setInlineMode(bool enabled) {
  if (mInlineMode == enabled) return;
  mInlineMode = enabled;
  // Move only the AI body out of its standalone scroll area. The enclosing
  // ClientNotesPage owns scrolling for the whole feed in inline mode.
  auto *outer = qobject_cast<QVBoxLayout *>(layout());
  if (enabled) {
    auto *scroll = qobject_cast<QScrollArea *>(mInlineBody->parentWidget()->parentWidget());
    if (scroll) {
      scroll->takeWidget(); outer->removeWidget(scroll);
      outer->insertWidget(1, mInlineBody); delete scroll;
    }
    mViewport = mInlineBody;
    mViewport->installEventFilter(this);
  } else {
    outer->removeWidget(mInlineBody);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(mInlineBody); outer->insertWidget(1, scroll, 1);
    mViewport = scroll->viewport(); mViewport->installEventFilter(this);
  }
  mManual->setVisible(!enabled); mSaved->setVisible(!enabled);
  findChild<QLabel *>("callNotesManualTitle")->setVisible(!enabled);
  mCollapse->setVisible(enabled); mCollapse->setChecked(true); mInlineBody->show();
  mEdit->setVisible(!enabled);
  refresh();
}

bool CallNotesPanel::eventFilter(QObject *object, QEvent *event) {
  if ((object == mDraftCard || object == mViewport) &&
      (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
    arrangeActions();
  return QWidget::eventFilter(object, event);
}

void CallNotesPanel::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() != QEvent::PaletteChange && event->type() != QEvent::StyleChange) return;
  updatePresentationColors();
}

void CallNotesPanel::updatePresentationColors() {
  for (const auto *name : {"callNotesManualTitle", "callNotesSaveStatus", "callNotesCoverage", "callNotesPointCount"}) {
    if (auto *text = findChild<QLabel *>(name)) {
      text->setForegroundRole(QPalette::WindowText);
      auto colors = text->palette(); colors.setColor(QPalette::WindowText, noteForeground(true)); text->setPalette(colors);
    }
  }
  const auto outline = QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 6px; padding: 4px 8px; } QPushButton:disabled { color: %3; }")
      .arg(noteForeground().name(), noteForeground(true).name(), noteForeground(false, true).name());
  for (const auto *name : {"callNotesEdit", "callNotesDismiss"})
    if (auto *button = findChild<QPushButton *>(name); button && button->styleSheet() != outline) button->setStyleSheet(outline);
  for (auto *button : findChildren<QPushButton *>("callNotesSource")) {
    const auto sourceStyle = QStringLiteral("QPushButton { background: transparent; color: %1; border: none; padding: 0px; font-weight: 400; text-align: %2; } QPushButton:disabled { color: %3; }")
        .arg(noteAccent().name(), button->property("compactSource").toBool() ? QStringLiteral("right") : QStringLiteral("left"), noteForeground(false, true).name());
    if (button->styleSheet() != sourceStyle) button->setStyleSheet(sourceStyle);
  }
  if (auto *icon = findChild<QLabel *>("callNotesStatusIcon"))
    icon->setPixmap(noteIcon(NoteGlyph::Check).pixmap(QSize(16, 16)));
}

void CallNotesPanel::arrangeActions() {
  const auto margins = mDraftCard->layout()->contentsMargins();
  const int spacing = mActions->horizontalSpacing() >= 0 ? mActions->horizontalSpacing() :
      std::max(0, style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing));
  const int required = mAccept->minimumSizeHint().width() + mEdit->minimumSizeHint().width() +
      mDismiss->minimumSizeHint().width() + 2 * spacing;
  const auto bodyMargins = mDraftCard->parentWidget()->layout()->contentsMargins();
  // A wide button row can impose its own minimum width on the scroll body.
  // Use the viewport's available width to break that sizing feedback loop.
  const int available = mViewport->width() - bodyMargins.left() - bodyMargins.right() -
      margins.left() - margins.right();
  const bool narrow = required > available;
  if (narrow == mNarrowActions) return;
  mNarrowActions = narrow;
  for (auto *button : {mAccept, mEdit, mDismiss}) mActions->removeWidget(button);
  mActions->addWidget(mAccept, 0, 0, 1, narrow ? 2 : 1);
  mActions->addWidget(mEdit, narrow ? 1 : 0, narrow ? 0 : 1);
  mActions->addWidget(mDismiss, narrow ? 1 : 0, narrow ? 1 : 2);
}

void CallNotesPanel::populateSources(QVBoxLayout *layout, const QJsonObject &note, bool compact) {
  auto *link = makeSourceLink(mController, note, compact, this, [this](qint64 id) { emit sourceRequested(id); });
  if (link) layout->addWidget(link);
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
  const bool busy = mController->busy() || live == "queued" || final == "queued" ||
      live == "running" || final == "running";
  const bool current = mController->sourcesCurrent();
  const bool conflict = mEditing && draft != mEditOrigin;
  // One status line: the live state during the call, the final note's after it.
  // The footer only repeats nothing; it shows the quiet idle/ready summary.
  const auto state = mController->ended() ? final : live;
  const bool quiet = state == "idle" || state == "ready";
  auto *line = mController->ended() ? mFinalStatus : mLiveStatus;
  (line == mFinalStatus ? mLiveStatus : mFinalStatus)->hide();
  auto lineText = mController->statusText();
  if (hasDraft && !current) lineText = tr("The transcript changed. Generate the note again.");
  if (conflict) lineText = tr("The model draft changed. Your edit is kept below; cancel editing to review the new draft.");
  line->setText(lineText);
  const bool showLine = mMaterialId > 0 && (!quiet || (hasDraft && !current) || conflict);
  line->setVisible(showLine);
  mTitle->setText(mController->ended() && final == "ready" ? tr("Final meeting note") : tr("Model draft"));
  const auto processed = mController->processedThroughMs();
  mCoverage->setText(hasDraft ? tr("Through %1").arg(QStringLiteral("%1:%2").arg(processed / 60000)
      .arg(processed / 1000 % 60, 2, 10, QLatin1Char('0'))) : QString{});
  mStatus->setText(mMaterialId <= 0 ? tr("Open a call to view or write notes.")
                   : showLine ? QString{} : mController->statusText());
  mDraftText->setText((mEditing ? mEditOrigin : draft)["text"].toString()); mDraftText->setVisible(!mEditing);
  mEditor->setVisible(mEditing); mDraftCard->setVisible(hasDraft || mEditing); mEmpty->setVisible(!hasDraft && !mEditing && (state == "idle" || state == "running" || state == "queued"));
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
      auto *item = new QWidget(this); item->setObjectName("callNotesPointRow");
      auto *row = new QHBoxLayout(item); row->setContentsMargins(0, 4, 0, 4); row->setSpacing(12);
      row->addWidget(label(QStringLiteral("• ") + point["text"].toString(), "callNotesPoint", item), 1);
      auto *sources = new QVBoxLayout; sources->setSpacing(2);
      populateSources(sources, point, true); row->addLayout(sources);
      mPoints->addWidget(item);
    }
    mRenderedDraft = draft; mRenderedPoints = points; mRenderedSources = sources; setProperty("sourcesCurrent", current);
  }
  for (auto *source : findChildren<QPushButton *>("callNotesSource"))
    source->setEnabled(source->property("validPhraseId").toBool() && current && !conflict);
  mPointCount->setText(tr("%n points", nullptr, mPoints->count()));
  mPointCount->setVisible(mPoints->count() > 0);
  mPointsTitle->setVisible(mPoints->count() > 0);
  const bool editable = hasDraft && current && !busy && (!mEditing || draft == mEditOrigin);
  mAccept->setEnabled(editable && !false);
  mAccept->setText(false && hasDraft ?
      (mInlineMode ? tr("Added to editor") : tr("Added to my notes")) :
      (mInlineMode ? tr("To editor") : tr("To my notes")));
  mUsePoints->setVisible(mInlineMode && !points.isEmpty());
  mUsePoints->setEnabled(current && !busy);
  mEdit->setEnabled(editable); mEdit->setText(mEditing ? tr("Save edit") : tr("Edit"));
  mDismiss->setEnabled(mEditing || (hasDraft && !busy));
  mDismiss->setText({});
  mDismiss->setAccessibleName(mEditing ? tr("Cancel edit") : tr("Dismiss"));
  mAccept->setToolTip(mAccept->text()); mEdit->setToolTip(mEdit->text()); mDismiss->setToolTip(mDismiss->accessibleName());
  arrangeActions();
  mProgress->setVisible(busy); mCancel->setVisible(busy);
  const bool retryLive = live == "error" || live == "cancelled" || live == "interrupted" || live == "stale";
  mStart->setVisible(!mController->ended() && (!hasDraft || retryLive)); mStart->setEnabled(!busy);
  mStart->setText(live == "error" || live == "cancelled" || live == "interrupted" || live == "stale" ? tr("Retry notes") : tr("Start notes"));
  mFinal->setVisible(mController->ended()); mFinal->setEnabled(!busy && mController->transcriptId() > 0);
  mFinal->setText(final == "error" || final == "cancelled" || final == "interrupted" || final == "stale" ? tr("Retry final note") : tr("Generate final note"));
  auto *statusIcon = findChild<QLabel *>("callNotesStatusIcon");
  statusIcon->setPixmap(noteIcon(NoteGlyph::Check).pixmap(QSize(16, 16)));
  statusIcon->setVisible(mMaterialId > 0 && !mSaveFailed && !busy &&
      (mController->ended() ? final : live) == "ready");
  updatePresentationColors();
}
