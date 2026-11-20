#pragma once

#include <QSize>

namespace pcm::video {

struct CallLayout {
  int rows{0};
  int columns{0};
  bool pictureInPicture{false};
  QSize tileSize;
};

class CallLayoutStrategy final {
public:
  // availableSize excludes the stage's control and notes overlay bands.
  static CallLayout select(int participantCount, bool hasLocal, QSize availableSize);
};

} // namespace pcm::video
