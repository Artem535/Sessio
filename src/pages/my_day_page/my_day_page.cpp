#include "my_day_page.h"

#include <QHBoxLayout>
#include <QQmlContext>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>

#include "my_day_bridge.h"

// Q_INIT_RESOURCE must run at global scope (the generated initializer is not namespaced).
static void initMyDayResources() { Q_INIT_RESOURCE(my_day_qml); }

namespace pcm::my_day {

MyDayPage::MyDayPage(std::shared_ptr<pcm::database::Database> db, QTimelineModel *model,
                     pcm::meeting::SeriesCallService *seriesCalls, QWidget *parent)
    : QWidget(parent) {
  initMyDayResources();
  if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  }
  mBridge = new MyDayBridge(std::move(db), model, seriesCalls, {}, this);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  mQuick = new QQuickWidget(this);
  mQuick->setObjectName("myDayQuickWidget");
  mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
  mQuick->setClearColor(palette().color(QPalette::Window));
  mQuick->rootContext()->setContextProperty("myDayBridge", mBridge);
  layout->addWidget(mQuick, 1);
  mQuick->setSource(QUrl("qrc:/sessio/myday/MyDay.qml"));
}

void MyDayPage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  mBridge->setActive(true);
  mBridge->reload();
}

void MyDayPage::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  mBridge->setActive(false);
}

} // namespace pcm::my_day
