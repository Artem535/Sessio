#pragma once

#include <QWidget>

#include <memory>

#include "database.h"

class QQuickWidget;
class QTimelineModel;

namespace pcm::meeting {
class SeriesCallService;
}

namespace pcm::my_day {

class MyDayBridge;

// «Мой день» home screen: a QQuickWidget hosting MyDay.qml on top of MyDayBridge.
class MyDayPage final : public QWidget {
  Q_OBJECT
public:
  MyDayPage(std::shared_ptr<pcm::database::Database> db, QTimelineModel *model,
            pcm::meeting::SeriesCallService *seriesCalls, QWidget *parent = nullptr);

  MyDayBridge *bridge() const { return mBridge; }

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  MyDayBridge *mBridge = nullptr;
  QQuickWidget *mQuick = nullptr;
};

} // namespace pcm::my_day
