#include "call_side_panel.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <oclero/qlementine/widgets/SegmentedControl.hpp>

CallSidePanel::CallSidePanel(QWidget *notesPage, QWidget *transcriptPanel, QWidget *parent)
    : QWidget(parent),
      mSwitcher(new oclero::qlementine::SegmentedControl(this)),
      mStack(new QStackedWidget(this)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  mSwitcher->addItem(tr("Notes"));
  mSwitcher->addItem(tr("Transcript"));
  mSwitcher->setItemsShouldExpand(true);
  mStack->addWidget(notesPage);
  mStack->addWidget(transcriptPanel);
  mSwitcher->setCurrentIndex(0);
  mStack->setCurrentIndex(0);
  layout->addWidget(mSwitcher);
  layout->addWidget(mStack, 1);

  connect(mSwitcher, &oclero::qlementine::SegmentedControl::currentIndexChanged,
          this, [this] {
    const int index = mSwitcher->currentIndex();
    if (index != 0 && (!mTranscriptionAvailable || (index != 1 && (index != 2 || mStack->count() < 3)))) {
      showNotes();
      return;
    }
    mStack->setCurrentIndex(index);
  });
  connect(mStack, &QStackedWidget::currentChanged, this, [this](int index) {
    emit pageChanged(index == 1);
  });
}

void CallSidePanel::setTranscriptionAvailable(bool available) {
  mTranscriptionAvailable = available;
  mSwitcher->setVisible(available);
  if (!available)
    showNotes();
}

void CallSidePanel::showNotes() { mSwitcher->setCurrentIndex(0); }

void CallSidePanel::showTranscript() {
  if (mTranscriptionAvailable)
    mSwitcher->setCurrentIndex(1);
}

bool CallSidePanel::transcriptShown() const { return mStack->currentIndex() == 1; }
void CallSidePanel::setPrompterPanel(QWidget *panel, QWidget *sharedHint) {
  if (mStack->count() >= 3) return;
  mStack->addWidget(panel);
  mSwitcher->addItem(tr("Prompter"));
  // The hint belongs to the Prompter tab. Keep its existing widget and signal
  // connections, and let the tab own its lifetime together with the cards.
  if (sharedHint && !panel->isAncestorOf(sharedHint)) {
    auto *body = panel->layout();
    if (!body) body = new QVBoxLayout(panel);
    body->addWidget(sharedHint);
  }
}
void CallSidePanel::setPrompterPendingCount(int count) {
  if (mStack->count() >= 3) mSwitcher->setItemBadge(2, count > 0 ? QString::number(count) : QString());
}
void CallSidePanel::showPrompter() {
  if (mTranscriptionAvailable && mStack->count() >= 3) mSwitcher->setCurrentIndex(2);
}
