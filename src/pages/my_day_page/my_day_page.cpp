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

MyDayPage::~MyDayPage() {
  // The bridge is the QML scene's context property and must outlive the view:
  // base-class child cleanup deletes children in creation order (bridge first),
  // and re-evaluated bindings in the still-live scene would create a font
  // engine after ~QApplication has cleared the global instance (qFatal).
  delete mQuick;
  mQuick = nullptr;
}

void MyDayPage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  // setActive(true) reloads by itself when data changed while hidden; reload here only otherwise.
  if (!mBridge->setActive(true)) {
    mBridge->reload();
  }
}

void MyDayPage::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  mBridge->setActive(false);
}

} // namespace pcm::my_day
