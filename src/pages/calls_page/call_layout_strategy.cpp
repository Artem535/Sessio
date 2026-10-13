#include "call_layout_strategy.h"

#include <algorithm>
#include <cmath>

namespace pcm::video {

CallLayout CallLayoutStrategy::select(int count, bool hasLocal, QSize available) {
  if (count <= 0 || available.width() <= 0 || available.height() <= 0)
    return {};
  const bool pip = count == 2 && hasLocal;
  if (pip)
    count = 1;
  CallLayout best;
  double bestWidth = -1;
  for (int columns = 1; columns <= count; ++columns) {
    const int rows = (count + columns - 1) / columns;
    const double width = std::max(0.0, std::min(
        double(available.width() - 8 * (columns - 1)) / columns,
        double(available.height() - 8 * (rows - 1)) / rows * 16 / 9));
    if (width > bestWidth) {
      bestWidth = width;
      best = {rows, columns, pip,
              QSize(int(std::floor(width)), int(std::floor(width * 9 / 16)))};
    }
  }
  return best;
}

} // namespace pcm::video
