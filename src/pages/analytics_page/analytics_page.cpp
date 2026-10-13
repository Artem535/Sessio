#include "analytics_page.h"

#include "qcustomplot.h"
#include "../../widgets/accent_color.h"
#include "../../widgets/app_settings.h"
#include "../../widgets/constants.hpp"
#include "../../widgets/surface_paint_filter.h"

#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/widgets/SegmentedControl.hpp>

#include <algorithm>
#include <QApplication>
#include <QDate>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
const QColor kAxisColor(255, 255, 255, 110);
const QColor kTextColor(255, 255, 255, 215);
const QColor kMutedTextColor(255, 255, 255, 140);
const QColor kGridColor(255, 255, 255, 26);
const QColor kIncomeColor(0x43, 0xc2, 0x7a);
// Distinct from the (purple) accent used for work bars.
const QColor kPersonalColor(0xf0, 0xb2, 0x7a);

QFrame *makeSurface(QWidget *parent = nullptr) {
  auto *frame = new QFrame(parent);
  frame->setObjectName("analyticsSurface");
  frame->setStyleSheet(
      "#analyticsSurface {"
      " background: rgba(255, 255, 255, 0.05);"
      " border: 1px solid rgba(255, 255, 255, 0.08);"
      " border-radius: 14px;"
      "}");
  return frame;
}

QString formatCurrency(const double value) {
  const auto rounded = qRound64(value);
  return QLocale().toString(rounded) + QStringLiteral(" ") +
         pcm::app_settings::currencySymbol();
}

int monthsBetweenInclusive(const QDate &startMonth, const QDate &endMonth) {
  return (endMonth.year() - startMonth.year()) * 12 +
         (endMonth.month() - startMonth.month()) + 1;
}

QDate firstMonthForStats(
    const std::vector<pcm::database::DashboardMonthlyStats> &stats,
    const int monthsBack) {
  const auto currentDate = QDate::currentDate();
  const auto currentMonth = QDate(currentDate.year(), currentDate.month(), 1);
  if (monthsBack > 0 || stats.empty()) {
    return currentMonth.addMonths(-(std::max(1, monthsBack) - 1));
  }

  const auto firstStats =
      std::min_element(stats.begin(), stats.end(), [](const auto &left, const auto &right) {
        return std::tie(left.year, left.month) < std::tie(right.year, right.month);
      });
  return QDate(firstStats->year, firstStats->month, 1);
}
} // namespace

AnalyticsPage::AnalyticsPage(std::shared_ptr<pcm::database::Database> db,
                             QWidget *parent)
    : QWidget(parent), mDb(std::move(db)) {
  buildUi();
  refresh();
}

void AnalyticsPage::refresh() {
  if (!mDb) {
    return;
  }

  const auto summary = mDb->get_dashboard_summary();
  const auto stats = mDb->get_dashboard_monthly_stats(selectedMonthsBack());

  updateSummaryCards(summary);
  updateIncomePlot(stats);
  updateMixPlot(stats);
}

void AnalyticsPage::buildUi() {
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  auto *scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *content = new QWidget(scrollArea);
  auto *rootLayout = new QVBoxLayout(content);
  rootLayout->setContentsMargins(pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding,
                                 pcm::widgets::constants::kPanelPadding);
  rootLayout->setSpacing(pcm::widgets::constants::kPanelPadding);

  auto *summaryGrid = new QGridLayout();
  summaryGrid->setContentsMargins(0, 0, 0, 0);
  summaryGrid->setHorizontalSpacing(pcm::widgets::constants::kPanelPadding);
  summaryGrid->setVerticalSpacing(pcm::widgets::constants::kPanelPadding);

  const auto makeCard = [this](const QString &caption) -> SummaryCard {
    auto *surface = makeSurface(this);
    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(6);

    auto *valueLabel = new QLabel("0", surface);
    auto valueFont = valueLabel->font();
    valueFont.setPointSize(valueFont.pointSize() + 8);
    valueFont.setBold(true);
    valueLabel->setFont(valueFont);
    valueLabel->setStyleSheet("color: rgba(255, 255, 255, 0.95);");

    auto *captionLabel = new QLabel(caption, surface);
    captionLabel->setStyleSheet("color: rgba(255, 255, 255, 0.58);");

    layout->addWidget(valueLabel);
    layout->addWidget(captionLabel);
    return SummaryCard{valueLabel, captionLabel};
  };

  mClientsCard = makeCard(tr("Total clients"));
  summaryGrid->addWidget(mClientsCard.value->parentWidget(), 0, 0);
  mActiveClientsCard = makeCard(tr("Active clients"));
  summaryGrid->addWidget(mActiveClientsCard.value->parentWidget(), 0, 1);
  mSessionsCard = makeCard(tr("Sessions this month"));
  summaryGrid->addWidget(mSessionsCard.value->parentWidget(), 0, 2);
  mIncomeCard = makeCard(tr("Income this month"));
  summaryGrid->addWidget(mIncomeCard.value->parentWidget(), 0, 3);

  for (int column = 0; column < 4; ++column) {
    summaryGrid->setColumnStretch(column, 1);
  }

  rootLayout->addLayout(summaryGrid);

  // DO NOT use makeSurface() (stylesheet) here — Qt cascades it through
  // QObject parent/child, wrapping style() in a QStyleSheetStyle proxy.
  // SegmentedControl paints via qobject_cast<QlementineStyle*>(style()),
  // which fails against that proxy and falls back to flat QPalette colors.
  // Paint the background via event filter to avoid any stylesheet on the parent.
  auto *periodSurface = new QFrame(content);
  periodSurface->installEventFilter(new pcm::widgets::SurfacePaintFilter(periodSurface));
  auto *periodLayout = new QHBoxLayout(periodSurface);
  periodLayout->setContentsMargins(16, 12, 16, 12);
  periodLayout->setSpacing(12);
  auto *periodLabel = new QLabel(tr("Period"), periodSurface);
  periodLabel->setStyleSheet("color: rgba(255, 255, 255, 0.68);");
  mPeriodControl = new oclero::qlementine::SegmentedControl(periodSurface);
  mPeriodControl->addItem(tr("6 months"), {}, {},
                          static_cast<int>(AnalyticsPeriod::Last6Months));
  mPeriodControl->addItem(tr("12 months"), {}, {},
                          static_cast<int>(AnalyticsPeriod::Last12Months));
  mPeriodControl->addItem(tr("All time"), {}, {},
                          static_cast<int>(AnalyticsPeriod::All));
  mPeriodControl->setItemsShouldExpand(true);
  mPeriodControl->setCurrentData(static_cast<int>(AnalyticsPeriod::Last6Months));
  connect(mPeriodControl, &oclero::qlementine::SegmentedControl::currentIndexChanged,
          this, &AnalyticsPage::refresh);
  periodLayout->addWidget(periodLabel, 0);
  periodLayout->addWidget(mPeriodControl, 1);
  rootLayout->addWidget(periodSurface);

  auto *incomeSurface = makeSurface(this);
  auto *incomeLayout = new QVBoxLayout(incomeSurface);
  incomeLayout->setContentsMargins(16, 14, 16, 16);
  incomeLayout->setSpacing(10);

  auto *incomeTitle = new QLabel(tr("Monthly income and sessions"), incomeSurface);
  auto titleFont = incomeTitle->font();
  titleFont.setPointSize(titleFont.pointSize() + 2);
  titleFont.setBold(true);
  incomeTitle->setFont(titleFont);
  incomeTitle->setStyleSheet("color: rgba(255, 255, 255, 0.88);");

  auto *incomeSubtitle = new QLabel(tr("Use the period switch to change chart range"), incomeSurface);
  incomeSubtitle->setStyleSheet("color: rgba(255, 255, 255, 0.55);");

  mIncomePlot = new QCustomPlot(incomeSurface);
  initPlot(mIncomePlot, tr("No events yet"));

  incomeLayout->addWidget(incomeTitle);
  incomeLayout->addWidget(incomeSubtitle);
  incomeLayout->addWidget(mIncomePlot, 1);
  rootLayout->addWidget(incomeSurface, 1);

  auto *mixSurface = makeSurface(this);
  auto *mixLayout = new QVBoxLayout(mixSurface);
  mixLayout->setContentsMargins(16, 14, 16, 16);
  mixLayout->setSpacing(10);

  auto *mixTitle = new QLabel(tr("Work vs personal sessions"), mixSurface);
  mixTitle->setFont(titleFont);
  mixTitle->setStyleSheet("color: rgba(255, 255, 255, 0.88);");

  auto *mixSubtitle = new QLabel(tr("Monthly breakdown by event type"), mixSurface);
  mixSubtitle->setStyleSheet("color: rgba(255, 255, 255, 0.55);");

  mMixPlot = new QCustomPlot(mixSurface);
  initPlot(mMixPlot, tr("No events yet"));

  mixLayout->addWidget(mixTitle);
  mixLayout->addWidget(mixSubtitle);
  mixLayout->addWidget(mMixPlot, 1);
  rootLayout->addWidget(mixSurface, 1);

  scrollArea->setWidget(content);
  outerLayout->addWidget(scrollArea);
}

int AnalyticsPage::selectedMonthsBack() const {
  if (!mPeriodControl) {
    return 6;
  }

  const auto period =
      static_cast<AnalyticsPeriod>(mPeriodControl->currentData().toInt());
  switch (period) {
  case AnalyticsPeriod::Last12Months:
    return 12;
  case AnalyticsPeriod::All:
    return 0;
  case AnalyticsPeriod::Last6Months:
  default:
    return 6;
  }
}

void AnalyticsPage::initPlot(QCustomPlot *plot, const QString &placeholderText) const {
  plot->setMinimumHeight(180);
  plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
  plot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
  plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
  plot->axisRect()->setMinimumMargins(QMargins(48, 16, 48, 48));
  plot->setBackground(Qt::NoBrush);
  plot->axisRect()->setBackground(Qt::transparent);
  plot->legend->setVisible(true);
  plot->legend->setBrush(Qt::NoBrush);
  plot->legend->setBorderPen(Qt::NoPen);
  plot->legend->setTextColor(kTextColor);

  plot->xAxis->setBasePen(QPen(kAxisColor));
  plot->xAxis->setTickPen(QPen(kAxisColor));
  plot->xAxis->setSubTickPen(QPen(kAxisColor));
  plot->xAxis->setTickLabelColor(kTextColor);
  plot->xAxis->setLabelColor(kTextColor);

  plot->yAxis->setBasePen(QPen(kAxisColor));
  plot->yAxis->setTickPen(QPen(kAxisColor));
  plot->yAxis->setSubTickPen(QPen(kAxisColor));
  plot->yAxis->setTickLabelColor(kTextColor);
  plot->yAxis->setLabelColor(kTextColor);
  plot->yAxis->grid()->setPen(QPen(kGridColor, 1, Qt::DashLine));
  plot->yAxis->grid()->setSubGridVisible(false);

  plot->yAxis2->setVisible(true);
  plot->yAxis2->setBasePen(QPen(kAxisColor));
  plot->yAxis2->setTickPen(QPen(kAxisColor));
  plot->yAxis2->setSubTickPen(QPen(kAxisColor));
  plot->yAxis2->setTickLabelColor(kMutedTextColor);
  plot->yAxis2->setLabelColor(kMutedTextColor);

  auto *placeholder = new QCPItemText(plot);
  placeholder->setPositionAlignment(Qt::AlignCenter);
  placeholder->position->setType(QCPItemPosition::ptAxisRectRatio);
  placeholder->position->setCoords(0.5, 0.48);
  placeholder->setText(placeholderText);
  placeholder->setColor(QColor(255, 255, 255, 110));
  placeholder->setFont(font());
  placeholder->setPadding(QMargins(8, 4, 8, 4));
  placeholder->setLayer("overlay");
  placeholder->setVisible(false);
  placeholder->setClipToAxisRect(false);
  placeholder->setProperty("placeholder", true);
}

void AnalyticsPage::updateSummaryCards(
    const pcm::database::DashboardSummary &summary) const {
  mClientsCard.value->setText(QLocale().toString(summary.total_clients));
  mActiveClientsCard.value->setText(QLocale().toString(summary.active_clients));
  mSessionsCard.value->setText(
      QStringLiteral("%1 / %2")
          .arg(QLocale().toString(summary.work_sessions_this_month),
               QLocale().toString(summary.sessions_this_month)));
  mSessionsCard.caption->setText(
      tr("Sessions this month") + QStringLiteral(" · ") + tr("work / total"));
  mIncomeCard.value->setText(formatCurrency(summary.income_this_month));
}

void AnalyticsPage::updateIncomePlot(
    const std::vector<pcm::database::DashboardMonthlyStats> &stats) const {
  QVector<double> ticks;
  QVector<double> sessions;
  QVector<double> income;
  QVector<QString> labels;

  const auto currentDate = QDate::currentDate();
  const auto currentMonth = QDate(currentDate.year(), currentDate.month(), 1);
  const auto firstMonth = firstMonthForStats(stats, selectedMonthsBack());
  const int monthsVisible =
      std::max(1, monthsBetweenInclusive(firstMonth, currentMonth));
  ticks.reserve(monthsVisible);
  sessions.reserve(monthsVisible);
  income.reserve(monthsVisible);
  labels.reserve(monthsVisible);

  for (int offset = 0; offset < monthsVisible; ++offset) {
    const auto monthDate = firstMonth.addMonths(offset);
    const auto tick = static_cast<double>(offset);
    ticks.push_back(tick);
    labels.push_back(QLocale().standaloneMonthName(monthDate.month(), QLocale::ShortFormat));

    double sessionsValue = 0.0;
    double incomeValue = 0.0;
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

  mIncomePlot->clearPlottables();
  auto *bars = new QCPBars(mIncomePlot->xAxis, mIncomePlot->yAxis);
  bars->setName(tr("Sessions"));
  bars->setWidth(0.55);
  bars->setPen(Qt::NoPen);
  bars->setBrush(pcm::widgets::accentColor());
  bars->setData(ticks, sessions);

  auto *line = mIncomePlot->addGraph(mIncomePlot->xAxis, mIncomePlot->yAxis2);
  line->setName(tr("Income"));
  line->setPen(QPen(kIncomeColor, 2.0));
  line->setScatterStyle(
      QCPScatterStyle(QCPScatterStyle::ssCircle, kIncomeColor, Qt::white, 6));
  line->setData(ticks, income);

  auto ticker = QSharedPointer<QCPAxisTickerText>::create();
  for (int i = 0; i < ticks.size(); ++i) {
    ticker->addTick(ticks.at(i), labels.at(i));
  }

  mIncomePlot->xAxis->setTicker(ticker);
  mIncomePlot->xAxis->setRange(-0.5, monthsVisible - 0.5);
  mIncomePlot->yAxis->setLabel(tr("Sessions"));
  mIncomePlot->yAxis2->setLabel(tr("Income"));

  double maxSessions = 1.0;
  double maxIncome = 1.0;
  for (int i = 0; i < sessions.size(); ++i) {
    maxSessions = std::max(maxSessions, sessions.at(i));
    maxIncome = std::max(maxIncome, income.at(i));
  }
  mIncomePlot->yAxis->setRange(0, maxSessions + 1.0);
  mIncomePlot->yAxis2->setRange(0, maxIncome > 0.0 ? maxIncome * 1.15 : 1.0);

  const bool hasData = std::any_of(stats.begin(), stats.end(), [](const auto &item) {
    return item.sessions > 0 || item.income > 0.0;
  });
  for (int i = 0; i < mIncomePlot->itemCount(); ++i) {
    auto *item = mIncomePlot->item(i);
    if (item && item->property("placeholder").toBool()) {
      item->setVisible(!hasData);
    }
  }

  mIncomePlot->replot();
}

void AnalyticsPage::updateMixPlot(
    const std::vector<pcm::database::DashboardMonthlyStats> &stats) const {
  QVector<double> ticks;
  QVector<double> workValues;
  QVector<double> personalValues;
  QVector<QString> labels;

  const auto currentDate = QDate::currentDate();
  const auto currentMonth = QDate(currentDate.year(), currentDate.month(), 1);
  const auto firstMonth = firstMonthForStats(stats, selectedMonthsBack());
  const int monthsVisible =
      std::max(1, monthsBetweenInclusive(firstMonth, currentMonth));
  ticks.reserve(monthsVisible);
  workValues.reserve(monthsVisible);
  personalValues.reserve(monthsVisible);
  labels.reserve(monthsVisible);

  for (int offset = 0; offset < monthsVisible; ++offset) {
    const auto monthDate = firstMonth.addMonths(offset);
    const auto tick = static_cast<double>(offset);
    ticks.push_back(tick);
    labels.push_back(QLocale().standaloneMonthName(monthDate.month(), QLocale::ShortFormat));

    double workValue = 0.0;
    double personalValue = 0.0;
    for (const auto &item : stats) {
      if (item.year == monthDate.year() && item.month == monthDate.month()) {
        workValue = static_cast<double>(item.work_sessions);
        personalValue = static_cast<double>(item.personal_sessions);
        break;
      }
    }

    workValues.push_back(workValue);
    personalValues.push_back(personalValue);
  }

  mMixPlot->clearPlottables();
  auto *workBars = new QCPBars(mMixPlot->xAxis, mMixPlot->yAxis);
  auto *personalBars = new QCPBars(mMixPlot->xAxis, mMixPlot->yAxis);
  auto *group = new QCPBarsGroup(mMixPlot);

  workBars->setName(tr("Work events"));
  workBars->setBrush(pcm::widgets::accentColor());
  workBars->setPen(Qt::NoPen);
  workBars->setWidth(0.24);
  workBars->setBarsGroup(group);

  personalBars->setName(tr("Personal events"));
  personalBars->setBrush(kPersonalColor);
  personalBars->setPen(Qt::NoPen);
  personalBars->setWidth(0.24);
  personalBars->setBarsGroup(group);

  workBars->setData(ticks, workValues);
  personalBars->setData(ticks, personalValues);

  auto ticker = QSharedPointer<QCPAxisTickerText>::create();
  for (int i = 0; i < ticks.size(); ++i) {
    ticker->addTick(ticks.at(i), labels.at(i));
  }

  mMixPlot->xAxis->setTicker(ticker);
  mMixPlot->xAxis->setRange(-0.5, monthsVisible - 0.5);
  mMixPlot->yAxis->setLabel(tr("Sessions"));
  mMixPlot->yAxis2->setLabel(QString());

  double maxValue = 1.0;
  for (int i = 0; i < workValues.size(); ++i) {
    maxValue = std::max(maxValue, workValues.at(i));
    maxValue = std::max(maxValue, personalValues.at(i));
  }
  mMixPlot->yAxis->setRange(0, maxValue + 1.0);
  mMixPlot->yAxis2->setRange(0, 1.0);

  const bool hasData = std::any_of(stats.begin(), stats.end(), [](const auto &item) {
    return item.work_sessions > 0 || item.personal_sessions > 0;
  });
  for (int i = 0; i < mMixPlot->itemCount(); ++i) {
    auto *item = mMixPlot->item(i);
    if (item && item->property("placeholder").toBool()) {
      item->setVisible(!hasData);
    }
  }

  mMixPlot->replot();
}
