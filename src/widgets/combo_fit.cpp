#include "combo_fit.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QScreen>
#include <QStyle>
#include <algorithm>

namespace pcm::widgets {

void fitComboToLongItems(QComboBox *combo) {
  if (!combo) return;
  combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo->setMinimumContentsLength(12);
  int widest = 0;
  const auto metrics = combo->view()->fontMetrics();
  for (int i = 0; i < combo->count(); ++i) {
    const QString text = combo->itemText(i);
    combo->setItemData(i, text, Qt::ToolTipRole);
    widest = std::max(widest, metrics.horizontalAdvance(text));
  }
  combo->setToolTip(combo->currentText());
  QObject::connect(combo, &QComboBox::currentTextChanged, combo, &QComboBox::setToolTip, Qt::UniqueConnection);
  // Room for the item padding, the check mark some styles draw, and a scroll bar.
  const int chrome = 4 * combo->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, combo) +
                     combo->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, combo) + 32;
  const auto *screen = combo->screen();
  const int limit = screen ? screen->availableGeometry().width() - 40 : 1200;
  combo->view()->setMinimumWidth(std::min(widest + chrome, limit));
}

} // namespace pcm::widgets
