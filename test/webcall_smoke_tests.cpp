#include "webcall_version.h"

#include <gtest/gtest.h>

// Proves QtWebEngine is linked and resolvable at runtime in every build
// flavour; later webcall tests build on the same library.
TEST(WebCallSmoke, ChromiumVersionIsKnown) {
  EXPECT_FALSE(pcm::webcall::webEngineChromiumVersion().isEmpty());
}
