// token-backend/test/health_controller_tests.cpp
#include "controller/health_controller.h"

#include <gtest/gtest.h>

TEST(HealthControllerTest, StatusTextReportsOk) {
  pcm::tokenbackend::HealthController controller;
  EXPECT_EQ(controller.statusText(), "ok");
}
