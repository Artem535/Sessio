#pragma once

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>
#include <string>

class QComboBox;
class QDialogButtonBox;
class QLabel;

namespace pcm::eventpage {

// Asks the specialist to confirm the timezone a recurring series will be
// pinned to before it is published (an existing series has none; a new one
// whose machine timezone the server would not accept needs a choice too).
//
// It shows the nearest dates of the series as they are today and as they would
// be in the chosen timezone. If any date would move, confirming is refused:
// pinning a timezone must never silently shift meetings that are already
// agreed with clients.
class SeriesTimezoneDialog final : public QDialog {
  Q_OBJECT

public:
  // The nearest occurrences if the series were pinned to `timezone` (an empty
  // string asks for the calendar the series has today); nullopt when the
  // timezone is unknown or the series cannot be expressed in it.
  using PreviewFn = std::function<std::optional<QVector<QDateTime>>(const std::string &timezone)>;

  SeriesTimezoneDialog(PreviewFn preview, const QString &initialTimezone,
                       QWidget *parent = nullptr);

  [[nodiscard]] QString selectedTimezone() const;
  void setSelectedTimezone(const QString &timezone);
  // Whether confirming is currently allowed.
  [[nodiscard]] bool canConfirm() const;

private:
  void refresh();
  [[nodiscard]] QString datesText(const std::optional<QVector<QDateTime>> &dates) const;

  PreviewFn mPreview;
  std::optional<QVector<QDateTime>> mCurrent;
  QComboBox *mCombo = nullptr;
  QLabel *mCurrentLabel = nullptr;
  QLabel *mNewLabel = nullptr;
  QLabel *mWarning = nullptr;
  QDialogButtonBox *mButtons = nullptr;
};

} // namespace pcm::eventpage
