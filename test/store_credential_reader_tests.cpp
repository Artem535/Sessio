#include "store_credential_reader.h"

#include "fake_token_backend_credential_store.h"

#include <QCoreApplication>
#include <QTest>
#include <gtest/gtest.h>

#include <memory>

// A store whose read completes only when the test says so, to model a locked
// or slow keychain.
class DeferredCredentialStore final : public TokenBackendCredentialStore {
  Q_OBJECT

public:
  using TokenBackendCredentialStore::TokenBackendCredentialStore;
  void readBearerCredential() override { ++reads; }
  void writeBearerCredential(const QString &) override {}
  void finish(bool ok, const QString &credential) { emit readFinished(ok, credential, ok ? QString() : "locked"); }
  int reads = 0;
};

TEST(StoreCredentialReaderTest, NothingIsReadWhenTheAdapterIsConstructed) {
  DeferredCredentialStore store;
  StoreCredentialReader adapter(store);
  EXPECT_EQ(store.reads, 0);
}

TEST(StoreCredentialReaderTest, LateKeychainAnswerReachesTheRequestThatAskedForIt) {
  DeferredCredentialStore store;
  StoreCredentialReader adapter(store);
  const auto read = adapter.reader();

  bool called = false;
  bool ok = false;
  QString credential;
  read([&](bool o, const QString &c) {
    called = true;
    ok = o;
    credential = c;
  });
  EXPECT_FALSE(called); // asynchronous: nothing yet
  EXPECT_EQ(store.reads, 1);

  store.finish(true, "bearer-late");
  EXPECT_TRUE(called);
  EXPECT_TRUE(ok);
  EXPECT_EQ(credential, "bearer-late");
}

TEST(StoreCredentialReaderTest, EveryRequestReadsAgainAndNeverReusesAnOldCredential) {
  DeferredCredentialStore store;
  StoreCredentialReader adapter(store);
  const auto read = adapter.reader();
  QStringList seen;
  const auto collect = [&](bool ok, const QString &c) { seen.append(ok ? c : QStringLiteral("<none>")); };

  read(collect);
  store.finish(true, "first");
  read(collect);
  EXPECT_EQ(store.reads, 2); // not served from a cache
  store.finish(false, {});   // keychain locked now
  read(collect);
  store.finish(true, "third");

  EXPECT_EQ(seen, (QStringList{"first", "<none>", "third"}));
}

TEST(StoreCredentialReaderTest, ConcurrentRequestsShareOneReadButAllGetAnAnswer) {
  DeferredCredentialStore store;
  StoreCredentialReader adapter(store);
  const auto read = adapter.reader();
  int answered = 0;
  read([&](bool ok, const QString &) { answered += ok; });
  read([&](bool ok, const QString &) { answered += ok; });
  EXPECT_EQ(store.reads, 1);
  store.finish(true, "x");
  EXPECT_EQ(answered, 2);
}

TEST(StoreCredentialReaderTest, EmptyCredentialCountsAsUnavailable) {
  FakeTokenBackendCredentialStore store; // answers synchronously, nothing stored
  StoreCredentialReader adapter(store);
  bool ok = true;
  adapter.reader()([&](bool o, const QString &) { ok = o; });
  EXPECT_FALSE(ok);

  store.writeBearerCredential("secret");
  adapter.reader()([&](bool o, const QString &c) { ok = o && c == "secret"; });
  EXPECT_TRUE(ok);
}

TEST(StoreCredentialReaderTest, CallableIsHarmlessAfterTheAdapterIsDestroyed) {
  DeferredCredentialStore store;
  auto adapter = std::make_unique<StoreCredentialReader>(store);
  const auto read = adapter->reader();
  adapter.reset();
  bool ok = true;
  read([&](bool o, const QString &) { ok = o; });
  EXPECT_FALSE(ok);
  EXPECT_EQ(store.reads, 0);
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "store_credential_reader_tests.moc"
