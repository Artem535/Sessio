#pragma once

#include <QWidget>

class QStackedWidget;
namespace oclero::qlementine { class SegmentedControl; }

class CallSidePanel final : public QWidget {
  Q_OBJECT
public:
  // Reparents both pages into its stack; QObject ownership manages their lifetime.
  CallSidePanel(QWidget *notesPage, QWidget *transcriptPanel, QWidget *parent = nullptr);
  void setTranscriptionAvailable(bool available);
  void showNotes();
  void showTranscript();
  [[nodiscard]] bool transcriptShown() const;
signals:
  void pageChanged(bool transcriptShown);
private:
  oclero::qlementine::SegmentedControl *mSwitcher;
  QStackedWidget *mStack;
  bool mTranscriptionAvailable = true;
};
