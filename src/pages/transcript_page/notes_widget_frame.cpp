#include "notes_widget_frame.h"
#include "surface_paint_filter.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

NotesWidgetFrame::NotesWidgetFrame(const QString &title, QWidget *parent) : QFrame(parent) {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 20, 24, 20);
  outer->setSpacing(14);
  auto *header = new QHBoxLayout;
  header->setSpacing(6);
  mTitle = new QLabel(title, this);
  mTitle->setObjectName("notesWidgetTitle");
  auto font = mTitle->font();
  font.setBold(true);
  mTitle->setFont(font);
  mStatus = new QLabel(this);
  mStatus->setObjectName("notesWidgetStatus");
  mStatus->setForegroundRole(QPalette::PlaceholderText);
  mStatus->hide();
  header->addWidget(mTitle, 1);
  header->addWidget(mStatus);
  outer->addLayout(header);
  mBody = new QVBoxLayout;
  mBody->setSpacing(6);
  outer->addLayout(mBody);
  mActionRow = new QWidget(this);
  mActionRow->setObjectName("notesWidgetActions");
  mActions = new QHBoxLayout(mActionRow);
  mActions->setContentsMargins(0, 0, 0, 0);
  mActions->setSpacing(6);
  mActions->addStretch();
  outer->addWidget(mActionRow);
  mActionRow->hide();
}

void NotesWidgetFrame::setTitle(const QString &title) { mTitle->setText(title); }

void NotesWidgetFrame::setStatus(const QString &text) {
  mStatus->setText(text);
  mStatus->setVisible(!text.isEmpty());
}

QPushButton *NotesWidgetFrame::addAction(const QString &text, const char *objectName) {
  auto *button = new QPushButton(text, mActionRow);
  button->setObjectName(QString::fromLatin1(objectName));
  button->setAutoDefault(false);
  mActions->insertWidget(mActions->count() - 1, button);
  mActionRow->show();
  return button;
}

QAction *NotesWidgetFrame::addMenuAction(const QString &text, const char *objectName) {
  if (!mMore) {
    mMenu = new QMenu(this);
    mMore = new QToolButton(mActionRow);
    mMore->setObjectName("notesWidgetMore");
    mMore->setText(QStringLiteral("⋯"));
    mMore->setAccessibleName(tr("More actions"));
    mMore->setToolTip(tr("More actions"));
    mMore->setPopupMode(QToolButton::InstantPopup);
    mMore->setMenu(mMenu);
    mActions->addWidget(mMore);
    mActionRow->show();
  }
  auto *action = mMenu->addAction(text);
  action->setObjectName(QString::fromLatin1(objectName));
  return action;
}
