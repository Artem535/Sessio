#include "client_charts_widget.h"

#include "qcustomplot.h"
#include "../../widgets/accent_color.h"
#include "../../widgets/constants.hpp"

#include <algorithm>
#include <QDate>
#include <QHBoxLayout>
#include <QLocale>
#include <QPen>

namespace {
constexpr auto kMonthsVisible = 6;
const QColor kIncomeColor(0x43, 0xc2, 0x7a);
const QColor kAxisColor(255, 255, 255, 110);
const QColor kTextColor(255, 255, 255, 215);
const QColor kGridColor(255, 255, 255, 26);
} // namespace

ClientChartsWidget::ClientChartsWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  mPlot = new QCustomPlot(this);
  layout->addWidget(mPlot);

  initPlot();
}

void ClientChartsWidget::setMonthlyStats(
    const std::vector<pcm::database::ClientMonthlyStats> &stats) {
  QVector<double> ticks;
  QVector<double> sessions;
  QVector<double> income;
  QVector<QString> labels;

  const auto currentDate = QDate::currentDate();
  const auto currentMonth = QDate(currentDate.year(), currentDate.month(), 1);

  ticks.reserve(kMonthsVisible);
  sessions.reserve(kMonthsVisible);
  income.reserve(kMonthsVisible);
  labels.reserve(kMonthsVisible);

  for (int offset = kMonthsVisible - 1; offset >= 0; --offset) {
    const auto monthDate = currentMonth.addMonths(-offset);
    const auto tick = static_cast<double>(kMonthsVisible - 1 - offset);
    ticks.push_back(tick);
    labels.push_back(QLocale().standaloneMonthName(monthDate.month(), QLocale::ShortFormat));

    auto sessionsValue = 0.0;
    auto incomeValue = 0.0;
    for (const auto &item : stats) {
      if (item.year == monthDate.year() && item.month == monthDate.month()) {
        sessionsValue = static_cast<double>(item.sessions);
        incomeValue = item.income;
        break;
      }
    }

    sessions.push_back(sessionsValue);
    income.push_back(incomeValue);
  }

  auto *bars = qobject_cast<QCPBars *>(mPlot->plottable(0));
  auto *line = qobject_cast<QCPGraph *>(mPlot->plottable(1));
  if (!bars || !line) {
    return;
  }

  bars->setData(ticks, sessions);
  line->setData(ticks, income);

  auto ticker = QSharedPointer<QCPAxisTickerText>::create();
  for (int i = 0; i < ticks.size(); ++i) {
    ticker->addTick(ticks.at(i), labels.at(i));
  }
  mPlot->xAxis->setTicker(ticker);
  mPlot->xAxis->setRange(-0.5, kMonthsVisible - 0.5);

  double maxSessions = 1.0;
  double maxIncome = 1.0;
  for (int i = 0; i < sessions.size(); ++i) {
    maxSessions = std::max(maxSessions, sessions.at(i));
    maxIncome = std::max(maxIncome, income.at(i));
  }

  mPlot->yAxis->setRange(0, maxSessions + 1.0);
  mPlot->yAxis2->setRange(0, maxIncome > 0.0 ? maxIncome * 1.15 : 1.0);

  updatePlaceholder(std::any_of(stats.begin(), stats.end(), [](const auto &item) {
    return item.sessions > 0 || item.income > 0.0;
  }));
  mPlot->replot();
}

void ClientChartsWidget::clear() {
  setMonthlyStats({});
}

void ClientChartsWidget::initPlot() {
  mPlot->setMinimumHeight(180);
  mPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
  mPlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
  mPlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
  mPlot->axisRect()->setMinimumMargins(QMargins(48, 16, 48, 48));
  mPlot->setBackground(Qt::NoBrush);
  mPlot->axisRect()->setBackground(Qt::transparent);
  mPlot->legend->setVisible(true);
  mPlot->legend->setBrush(Qt::NoBrush);
  mPlot->legend->setBorderPen(Qt::NoPen);
  mPlot->legend->setTextColor(kTextColor);

  mPlot->xAxis->setBasePen(QPen(kAxisColor));
  mPlot->xAxis->setTickPen(QPen(kAxisColor));
  mPlot->xAxis->setSubTickPen(QPen(kAxisColor));
  mPlot->xAxis->setTickLabelColor(kTextColor);
  mPlot->xAxis->setLabelColor(kTextColor);

  mPlot->yAxis->setBasePen(QPen(kAxisColor));
  mPlot->yAxis->setTickPen(QPen(kAxisColor));
  mPlot->yAxis->setSubTickPen(QPen(kAxisColor));
  mPlot->yAxis->setTickLabelColor(kTextColor);
  mPlot->yAxis->setLabelColor(kTextColor);
  mPlot->yAxis->grid()->setPen(QPen(kGridColor, 1, Qt::DashLine));
  mPlot->yAxis->grid()->setSubGridVisible(false);

  mPlot->yAxis2->setVisible(true);
  mPlot->yAxis2->setBasePen(QPen(kAxisColor));
  mPlot->yAxis2->setTickPen(QPen(kAxisColor));
  mPlot->yAxis2->setSubTickPen(QPen(kAxisColor));
  mPlot->yAxis2->setTickLabelColor(kTextColor);
  mPlot->yAxis2->setLabelColor(kTextColor);

  mPlot->xAxis->setLabel(tr("Months"));
  mPlot->yAxis->setLabel(tr("Sessions"));
  mPlot->yAxis2->setLabel(tr("Income"));

  auto *bars = new QCPBars(mPlot->xAxis, mPlot->yAxis);
  bars->setName(tr("Sessions"));
  bars->setWidth(0.55);
  bars->setPen(Qt::NoPen);
  bars->setBrush(pcm::widgets::accentColor());

  auto *line = mPlot->addGraph(mPlot->xAxis, mPlot->yAxis2);
  line->setName(tr("Income"));
  line->setPen(QPen(kIncomeColor, 2.0));
  line->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, kIncomeColor, Qt::white, 6));
  line->setBrush(Qt::NoBrush);

  auto *placeholder = new QCPItemText(mPlot);
  placeholder->setPositionAlignment(Qt::AlignCenter);
  placeholder->position->setType(QCPItemPosition::ptAxisRectRatio);
  placeholder->position->setCoords(0.5, 0.48);
  placeholder->setText(tr("No client activity yet"));
  placeholder->setColor(QColor(255, 255, 255, 110));
  placeholder->setFont(font());
  placeholder->setPadding(QMargins(8, 4, 8, 4));
  placeholder->setLayer("overlay");
  placeholder->setVisible(false);
  placeholder->setClipToAxisRect(false);
  placeholder->setProperty("placeholder", true);
}

void ClientChartsWidget::updatePlaceholder(const bool hasData) {
  for (int i = 0; i < mPlot->itemCount(); ++i) {
    auto *item = mPlot->item(i);
    if (item && item->property("placeholder").toBool()) {
      item->setVisible(!hasData);
    }
  }
}
