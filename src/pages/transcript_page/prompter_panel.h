#pragma once
#include "call_prompter_controller.h"
#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;
class PrompterPanel final : public QWidget {
  Q_OBJECT
public:
  PrompterPanel(CallPrompterController *controller, std::shared_ptr<pcm::database::Database> db, QWidget *parent = nullptr);
  QWidget *sharedHintWidget() const { return mHint; }
  CallPrompterController *controller() const { return mController; }
  bool controlsVisible() const;
  QString activeHint() const;
  void dismissHint();
  bool isCallNoteSaved(const QString &cardId) const { return mSavedCallNotes.contains(cardId); }
  void triggerStart();
  void triggerPause();
  void triggerCardAction(const QString &cardId, const QString &action);
  void refresh();
  // Card on the call notes page: no own scroll area and no accepted-agreements
  // list (the page shows those in its Agreements card).
  void setEmbedded(bool embedded);
  void setCallNoteSaver(std::function<bool(const QString &, const QString &)> saver) { mCallNoteSaver = std::move(saver); }
signals:
  void startRequested();
  void pauseRequested();
  void sourceRequested(qint64 phraseId);
  void noteSaved();
  void pendingCountChanged(int count);
  void stateChanged();
private:
  void saveNote(const QString &cardId);
  QPointer<CallPrompterController> mController;
  std::shared_ptr<pcm::database::Database> mDb;
  QLabel *mStatus;
  QScrollArea *mScroll;
  QList<QWidget *> mAcceptedSection;
  QWidget *mHint;
  QLabel *mHintText;
  QLabel *mRecords;
  QLabel *mEmpty;
  QPushButton *mStart;
  QPushButton *mPause;
  QVBoxLayout *mCards;
  QHash<QString, QWidget *> mCardWidgets;
  QString mDismissedHint;
  std::function<bool(const QString &, const QString &)> mCallNoteSaver;
  QSet<QString> mSavedCallNotes;
};
