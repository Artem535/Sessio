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
    if (index != 0 && (index != 1 || !mTranscriptionAvailable)) {
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
