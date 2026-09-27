#include "auth/static_token_authorizer.h"
#include "db/accounts_repository.h"
#include "db/migrations.h"
#include "db/sqlite_connection.h"

#include <gtest/gtest.h>
#include <sodium.h>

class StaticTokenAuthorizerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sodium_init() >= 0, true);
    conn = std::make_unique<pcm::tokenbackend::SqliteConnection>(":memory:");
    pcm::tokenbackend::runMigrations(*conn);
    accounts = std::make_unique<pcm::tokenbackend::AccountsRepository>(*conn);
    credential = accounts->seedAccount();
  }
  std::unique_ptr<pcm::tokenbackend::SqliteConnection> conn;
  std::unique_ptr<pcm::tokenbackend::AccountsRepository> accounts;
  std::string credential;
};

TEST_F(StaticTokenAuthorizerTest, AuthorizesValidCredential) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize(credential).has_value());
}

TEST_F(StaticTokenAuthorizerTest, RejectsInvalidCredential) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_FALSE(authorizer.authorize("garbage").has_value());
}

// RFC 7235 spells the header `Authorization: Bearer <credential>`. Hashing
// that literal string never matches, so a standards-compliant client used to
// get a silent 401 while only the non-standard bare form worked.

TEST_F(StaticTokenAuthorizerTest, AcceptsRfc7235BearerPrefix) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize("Bearer " + credential).has_value());
}

TEST_F(StaticTokenAuthorizerTest, BearerPrefixIsCaseInsensitive) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize("bearer " + credential).has_value());
  EXPECT_TRUE(authorizer.authorize("BEARER " + credential).has_value());
  EXPECT_TRUE(authorizer.authorize("BeArEr " + credential).has_value());
}

TEST_F(StaticTokenAuthorizerTest, ToleratesSurroundingAndInterveningWhitespace) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize("  Bearer   " + credential + "  ").has_value());
}

TEST_F(StaticTokenAuthorizerTest, StillAcceptsTheBareCredentialForm) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_TRUE(authorizer.authorize(credential).has_value());
}

TEST_F(StaticTokenAuthorizerTest, BearerPrefixDoesNotSmuggleAWrongCredential) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_FALSE(authorizer.authorize("Bearer garbage").has_value());
}

TEST_F(StaticTokenAuthorizerTest, RejectsEmptyAndSchemeOnlyHeaders) {
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_FALSE(authorizer.authorize("").has_value());
  EXPECT_FALSE(authorizer.authorize("Bearer ").has_value());
  EXPECT_FALSE(authorizer.authorize("   ").has_value());
}

TEST_F(StaticTokenAuthorizerTest, DoesNotTruncateACredentialThatStartsWithBearer) {
  // "bearer" without a following space is part of the credential, not a scheme.
  pcm::tokenbackend::StaticTokenAuthorizer authorizer(*accounts);
  EXPECT_EQ(pcm::tokenbackend::stripBearerPrefix("bearerish-credential"),
            "bearerish-credential");
}
