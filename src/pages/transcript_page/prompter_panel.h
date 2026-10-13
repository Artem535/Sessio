#pragma once
#include "call_prompter_controller.h"
#include <QWidget>
#include <QHash>
#include <QPointer>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;
class PrompterPanel final : public QWidget {
  Q_OBJECT
public:
  PrompterPanel(CallPrompterController *controller, std::shared_ptr<pcm::database::Database> db, QWidget *parent = nullptr);
  QWidget *sharedHintWidget() const { return mHint; }
  void refresh();
signals:
  void startRequested();
  void pauseRequested();
  void sourceRequested(qint64 phraseId);
  void noteSaved();
  void pendingCountChanged(int count);
private:
  void saveNote(const QString &cardId);
  QPointer<CallPrompterController> mController;
  std::shared_ptr<pcm::database::Database> mDb;
  QLabel *mStatus;
  QWidget *mHint;
  QLabel *mHintText;
  QLabel *mRecords;
  QLabel *mEmpty;
  QPushButton *mStart;
  QPushButton *mPause;
  QVBoxLayout *mCards;
  QHash<QString, QWidget *> mCardWidgets;
  QString mDismissedHint;
};
