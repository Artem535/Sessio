#pragma once

#include <QString>

namespace pcm::tokenclient {

struct TokenResult {
  QString endpointUrl;
  QString roomName;
  QString token;
  qint64 expiresAt = 0;
};

} // namespace pcm::tokenclient
