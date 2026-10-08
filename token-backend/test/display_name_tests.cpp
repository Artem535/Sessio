#include "service/display_name.h"
#include <gtest/gtest.h>
#ifdef PCM_DISPLAY_NAME_CASES
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include <fstream>
#include <sstream>
#endif
namespace pcm::tokenbackend {
#ifdef PCM_DISPLAY_NAME_CASES
#include OATPP_CODEGEN_BEGIN(DTO)
class DisplayNameCase : public oatpp::DTO {
  DTO_INIT(DisplayNameCase, DTO)
  DTO_FIELD(String, value);
  DTO_FIELD(Boolean, accepted);
  DTO_FIELD(String, normalised);
};
#include OATPP_CODEGEN_END(DTO)
TEST(DisplayName, MatchesSharedFrontendVectors) {
  std::ifstream input(PCM_DISPLAY_NAME_CASES); ASSERT_TRUE(input);
  std::ostringstream json; json << input.rdbuf();
  const auto mapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
  const auto cases = mapper->readFromString<oatpp::List<oatpp::Object<DisplayNameCase>>>(json.str().c_str());
  for (const auto &item : *cases) {
    const auto result = normaliseDisplayName(*item->value, true);
    EXPECT_EQ(result.has_value(), *item->accepted);
    if (result) EXPECT_EQ(*result, *item->normalised);
  }
}
#endif
TEST(DisplayName, RequiresWebNameButAcceptsLegacyEmpty) {
  EXPECT_FALSE(normaliseDisplayName("  ", true));
  EXPECT_EQ(normaliseDisplayName("", false), "");
}
TEST(DisplayName, CountsUnicodeScalarsAndTrimsUnicodeSpace) {
  EXPECT_EQ(normaliseDisplayName("\xC2\xA0Гость\xE3\x80\x80", true), "Гость");
  std::string name; for (int i = 0; i < 80; ++i) name += "😀";
  EXPECT_TRUE(normaliseDisplayName(name, true));
  EXPECT_FALSE(normaliseDisplayName(name + "я", true));
}
TEST(DisplayName, RejectsMalformedUtf8AndControls) {
  for (auto value : {"a\xC2\x85", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\x80"})
    EXPECT_FALSE(normaliseDisplayName(value, true));
}
}
