#include "jitsi_url.h"
#include <gtest/gtest.h>

TEST(JitsiUrlTest, PreservesDeploymentPrefixAndDecodedRoom) {
  const auto address = pcm::meeting::parseJitsiRoomUrl("https://meet.example.org/meet/room%20one/");
  ASSERT_TRUE(address);
  EXPECT_EQ(address->server.toString(), "https://meet.example.org/meet/");
  EXPECT_EQ(address->room, "room one");
}
TEST(JitsiUrlTest, RejectsCredentialsParametersAndNonSecureRemoteHosts) {
  for (const auto *url : {"http://meet.example.org/room", "https://user@meet.example.org/room",
       "https://meet.example.org/room?jwt=secret", "https://meet.example.org/room#secret",
       "https://meet.example.org/", "https://meet.example.org/a%2Fb", "file:///room"})
    EXPECT_FALSE(pcm::meeting::parseJitsiRoomUrl(url)) << url;
  EXPECT_TRUE(pcm::meeting::parseJitsiRoomUrl("http://127.0.0.1:8000/room"));
  EXPECT_TRUE(pcm::meeting::parseJitsiRoomUrl("http://127.0.0.2:8000/room"));
  EXPECT_TRUE(pcm::meeting::parseJitsiRoomUrl("http://[::1]:8000/room"));
}
TEST(JitsiUrlTest, CreatesDistinctRoomsUnderSelectedServer) {
  const auto server = pcm::meeting::parseJitsiServerUrl("https://meet.example.org/prefix");
  ASSERT_TRUE(server);
  const auto first = pcm::meeting::createJitsiRoomUrl(*server);
  EXPECT_NE(first, pcm::meeting::createJitsiRoomUrl(*server));
  const auto parsed = pcm::meeting::parseJitsiRoomUrl(first);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->server, *server);
}
