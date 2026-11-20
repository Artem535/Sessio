#include "webcall_version.h"

#include <QtWebEngineCore/qtwebenginecoreglobal.h>

namespace pcm::webcall {

QString webEngineChromiumVersion() {
  return QString::fromLatin1(qWebEngineChromiumVersion());
}

} // namespace pcm::webcall
