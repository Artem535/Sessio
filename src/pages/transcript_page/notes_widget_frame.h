#pragma once

#include <QFrame>

class QAction;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// One card of the call notes page: title, status chip, body, labelled
// actions and an optional «⋯» menu for rare or destructive actions.
class NotesWidgetFrame : public QFrame {
  Q_OBJECT
public:
  explicit NotesWidgetFrame(const QString &title, QWidget *parent = nullptr);
  void setTitle(const QString &title);
  // Empty text hides the chip.
  void setStatus(const QString &text);
  QVBoxLayout *body() const { return mBody; }
  // Labelled button in the action row (left to right, row hidden while empty).
  QPushButton *addAction(const QString &text, const char *objectName);
  // Entry in the «⋯» menu; the «⋯» button exists only once a menu action is added.
  QAction *addMenuAction(const QString &text, const char *objectName);

private:
  QLabel *mTitle;
  QLabel *mStatus;
  QVBoxLayout *mBody;
  QWidget *mActionRow;
  QHBoxLayout *mActions;
  QToolButton *mMore = nullptr;
  QMenu *mMenu = nullptr;
};
