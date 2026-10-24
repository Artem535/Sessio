#pragma once

#include "notes_widget_frame.h"
#include <QJsonArray>
#include <QPointer>

class CallNotesController;

// «Key points» card over the call notes controller.
class KeyPointsWidget final : public NotesWidgetFrame {
  Q_OBJECT
public:
  explicit KeyPointsWidget(CallNotesController *controller, QWidget *parent = nullptr);
  bool hasContent() const;  // at least one visible point

signals:
  void sourceRequested(qint64 phraseId);
  void contentChanged();

private:
  void refresh();
  QPointer<CallNotesController> mController;
  QVBoxLayout *mRows;
  QJsonArray mRendered;
  QString mRenderedSummary;
  bool mRenderedCurrent = true;
  int mCount = 0;
};
