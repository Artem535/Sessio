#include "key_points_widget.h"
#include "call_notes_controller.h"
#include "call_notes_sources.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

KeyPointsWidget::KeyPointsWidget(CallNotesController *controller, QWidget *parent)
    : NotesWidgetFrame(tr("Key points"), parent), mController(controller) {
  mRows = new QVBoxLayout; mRows->setSpacing(4); body()->addLayout(mRows);
  if (controller) connect(controller, &CallNotesController::changed, this, &KeyPointsWidget::refresh);
  refresh();
}

bool KeyPointsWidget::hasContent() const { return mCount > 0; }

void KeyPointsWidget::refresh() {
  if (!mController) return;
  const auto points = mController->keyPoints();
  const auto summary = mController->draft()["text"].toString().trimmed();
  const bool current = mController->sourcesCurrent();
  if (mRendered == points && mRenderedSummary == summary && mRenderedCurrent == current) return;
  mRendered = points; mRenderedSummary = summary; mRenderedCurrent = current;
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mCount = 0;
  for (const auto &value : points) {
    const auto point = value.toObject();
    const auto text = point["text"].toString().trimmed();
    if (text.isEmpty() || text == summary) continue;
    auto *row = new QWidget(this); row->setObjectName("keyPointRow");
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 2, 0, 2); layout->setSpacing(8);
    auto *bullet = new QLabel(QStringLiteral("•"), row);
    bullet->setObjectName("keyPointBullet"); bullet->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    bullet->setFixedWidth(12);
    layout->addWidget(bullet, 0, Qt::AlignTop);
    auto *label = new QLabel(text, row); label->setObjectName("keyPointText");
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label, 1);
    if (auto *link = makeSourceLink(mController, point, true, row, [this](qint64 id) { emit sourceRequested(id); }))
      layout->addWidget(link, 0, Qt::AlignTop);
    mRows->addWidget(row); ++mCount;
  }
  setStatus(mCount > 0 ? QString::number(mCount) : QString{});
  emit contentChanged();
}
