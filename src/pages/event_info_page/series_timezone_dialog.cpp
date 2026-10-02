#include "series_timezone_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>

namespace pcm::eventpage {

SeriesTimezoneDialog::SeriesTimezoneDialog(PreviewFn preview, const QString &initialTimezone,
                                           QWidget *parent)
    : QDialog(parent), mPreview(std::move(preview)) {
  setWindowTitle(tr("Confirm the series timezone"));
  setModal(true);
  auto *layout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      tr("A permanent link needs a fixed timezone for this series. From now on the meeting times "
         "follow this timezone, not the timezone of this computer. Check that the dates below "
         "stay the same."),
      this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  mCombo = new QComboBox(this);
  mCombo->setObjectName(QStringLiteral("seriesTimezoneCombo"));
  mCombo->setEditable(false);
  mCombo->addItem(tr("Choose a timezone"), QString{});
  auto ids = QTimeZone::availableTimeZoneIds();
  std::sort(ids.begin(), ids.end());
  for (const auto &id : ids) {
    mCombo->addItem(QString::fromLatin1(id), QString::fromLatin1(id));
  }
  layout->addWidget(mCombo);

  mCurrentLabel = new QLabel(this);
  mCurrentLabel->setObjectName(QStringLiteral("seriesTimezoneCurrentDates"));
  mCurrentLabel->setWordWrap(true);
  mNewLabel = new QLabel(this);
  mNewLabel->setObjectName(QStringLiteral("seriesTimezoneNewDates"));
  mNewLabel->setWordWrap(true);
  mWarning = new QLabel(this);
  mWarning->setObjectName(QStringLiteral("seriesTimezoneWarning"));
  mWarning->setWordWrap(true);
  mWarning->setStyleSheet(QStringLiteral("color: #f0c36d;"));
  layout->addWidget(mCurrentLabel);
  layout->addWidget(mNewLabel);
  layout->addWidget(mWarning);

  mButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(mButtons);
  connect(mButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(mButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  mCurrent = mPreview(std::string{});
  connect(mCombo, &QComboBox::currentIndexChanged, this, [this]() { refresh(); });
  setSelectedTimezone(initialTimezone);
  refresh();
}

QString SeriesTimezoneDialog::selectedTimezone() const {
  return mCombo->currentData().toString();
}

void SeriesTimezoneDialog::setSelectedTimezone(const QString &timezone) {
  const int index = mCombo->findData(timezone);
  mCombo->setCurrentIndex(index >= 0 ? index : 0);
}

bool SeriesTimezoneDialog::canConfirm() const {
  return mButtons->button(QDialogButtonBox::Ok)->isEnabled();
}

QString SeriesTimezoneDialog::datesText(const std::optional<QVector<QDateTime>> &dates) const {
  if (!dates.has_value()) {
    return tr("not available");
  }
  if (dates->isEmpty()) {
    return tr("no upcoming dates");
  }
  QStringList parts;
  for (const auto &date : *dates) {
    parts << QLocale().toString(date.toLocalTime(), QLocale::ShortFormat);
  }
  return parts.join(QStringLiteral("; "));
}

void SeriesTimezoneDialog::refresh() {
  const auto timezone = selectedTimezone();
  mCurrentLabel->setText(tr("Dates today: %1").arg(datesText(mCurrent)));
  auto *ok = mButtons->button(QDialogButtonBox::Ok);
  if (timezone.isEmpty()) {
    mNewLabel->setText({});
    mWarning->setText(tr("Choose the timezone this series is scheduled in."));
    ok->setEnabled(false);
    return;
  }
  const auto preview = mPreview(timezone.toStdString());
  mNewLabel->setText(tr("Dates in %1: %2").arg(timezone, datesText(preview)));
  if (!preview.has_value()) {
    mWarning->setText(tr("This timezone cannot be used for the series."));
    ok->setEnabled(false);
    return;
  }
  const bool same = mCurrent.has_value() && mCurrent->size() == preview->size() &&
                    std::equal(mCurrent->cbegin(), mCurrent->cend(), preview->cbegin(),
                               [](const QDateTime &a, const QDateTime &b) {
                                 return a.toSecsSinceEpoch() / 60 == b.toSecsSinceEpoch() / 60;
                               });
  mWarning->setText(same ? QString{}
                         : tr("Some meetings would move to other dates or times. Pick another "
                              "timezone, or cancel and keep the series as it is."));
  ok->setEnabled(same);
}

} // namespace pcm::eventpage
