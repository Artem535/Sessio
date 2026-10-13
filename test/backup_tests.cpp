#include <Poco/DirectoryIterator.h>
#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/Zip/Compress.h>
#include <Poco/Zip/Decompress.h>
#include <Poco/Zip/ZipCommon.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <rfl/json.hpp>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <sodium.h>

#include "auto_backup_due.h"
#include "auto_backup_scheduler.h"
#include "backup_encryption_policy.h"
#include "backup_manifest.hpp"
#include "backup_rotation_service.h"
#include "backup_service.h"
#include "backup_validator.h"
#include "checksum_utils.hpp"
#include "config.h"
#include "credential_store.h"
#include "database.h"
#include "encrypted_container.h"
#include "restore_service.h"
#include "app_settings.h"
#include "app_lock_service.h"
#include "app_lock_controller.h"

namespace {

std::string writeTempFile(const std::string &name, const std::string &content) {
  const auto path = Poco::Path(Poco::Path::current()).append(name).toString();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
  out.close();
  return path;
}

void overwriteFile(const std::string &path, const std::string &content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
  out.close();
}

std::string tempPath(const std::string &name) {
  return Poco::Path(Poco::Path::current()).append(name).toString();
}

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

void removeIfExists(const std::string &path) {
  Poco::File file(path);
  if (file.exists()) {
    file.remove();
  }
}

pcm::backup::MasterKey fixedMasterKey() {
  pcm::backup::MasterKey key;
  for (std::size_t index = 0; index < key.bytes.size(); ++index) {
    key.bytes[index] = static_cast<unsigned char>(index);
  }
  return key;
}

std::string testRecoveryPhrase() {
  return std::string(24, 'r');
}

std::string otherRecoveryPhrase() {
  return std::string(24, 'x');
}

bool createEncryptedBackup(const pcm::database::Database &database,
                           const std::string &backup_path) {
  pcm::backup::BackupOptions options;
  options.encryption = pcm::backup::BackupEncryptionOptions{
      .master_key = fixedMasterKey(),
      .recovery_password = testRecoveryPhrase()};
  return pcm::backup::BackupService{}.create_backup(database, backup_path, options).ok;
}

pcm::backup::RestoreResult restoreWithRecoveryPhrase(const std::string &backup_path,
                                                     const std::string &target_path) {
  pcm::backup::RestoreOptions options;
  options.recovery_password = testRecoveryPhrase();
  return pcm::backup::RestoreService{}.restore_backup(backup_path, target_path, options);
}

pcm::backup::RecoveryEnvelope recoveryEnvelope() {
  pcm::backup::RecoveryEnvelope envelope;
  const auto result = pcm::backup::create_recovery_envelope(
      testRecoveryPhrase(), fixedMasterKey(), &envelope);
  if (!result.ok) {
    throw std::runtime_error(result.error);
  }
  return envelope;
}

constexpr std::string_view kLegacyContainerMagic = "PCMENC01";
constexpr std::uint32_t kLegacyContainerVersion = 1;
constexpr std::uint32_t kLegacyChunkSize = 64 * 1024;

TEST(AppLockServiceTest, VerifiesConfiguredPinAndRejectsDifferentPin) {
  QSettings settings{"SessioTest", "AppLockServiceTest"};
  settings.clear();
  pcm::AppLockService service{&settings};

  ASSERT_TRUE(service.configure("123456"));
  EXPECT_TRUE(service.verify("123456"));
  EXPECT_FALSE(service.verify("654321"));
  EXPECT_TRUE(service.isConfigured());
}

TEST(AppLockControllerTest, LocksAfterConfiguredIdleTimeout) {
  pcm::AppLockController controller{1};
  controller.recordActivity(0);
  EXPECT_FALSE(controller.shouldLock(59'999));
  EXPECT_TRUE(controller.shouldLock(60'000));
}

TEST(AppLockControllerTest, KeepsManualLockUntilExplicitUnlock) {
  pcm::AppLockController controller{0};
  controller.recordActivity(0);
  controller.lockNow();

  EXPECT_TRUE(controller.isLocked());
  controller.recordActivity(10);
  EXPECT_TRUE(controller.isLocked());

  controller.unlock(20);
  EXPECT_FALSE(controller.isLocked());
  EXPECT_FALSE(controller.shouldLock(20));
}

struct LegacyHeaderForWrapFixture {
  std::uint32_t container_version = kLegacyContainerVersion;
  std::uint32_t kdf_algorithm = crypto_pwhash_ALG_ARGON2ID13;
  std::uint64_t kdf_opslimit = crypto_pwhash_OPSLIMIT_MODERATE;
  std::uint64_t kdf_memlimit = crypto_pwhash_MEMLIMIT_MODERATE;
  std::string salt;
  std::string wrap_nonce;
  std::string stream_header;
  std::uint32_t chunk_size = kLegacyChunkSize;
};

struct LegacyContainerHeaderFixture {
  std::uint32_t container_version = kLegacyContainerVersion;
  std::uint32_t kdf_algorithm = crypto_pwhash_ALG_ARGON2ID13;
  std::uint64_t kdf_opslimit = crypto_pwhash_OPSLIMIT_MODERATE;
  std::uint64_t kdf_memlimit = crypto_pwhash_MEMLIMIT_MODERATE;
  std::string salt;
  std::string wrap_nonce;
  std::string stream_header;
  std::uint32_t chunk_size = kLegacyChunkSize;
  std::string wrapped_master_key;
};

template <std::size_t Size>
std::string base64Encode(const std::array<unsigned char, Size> &bytes) {
  std::array<char, sodium_base64_ENCODED_LEN(Size, sodium_base64_VARIANT_ORIGINAL)>
      encoded{};
  sodium_bin2base64(encoded.data(), encoded.size(), bytes.data(), bytes.size(),
                    sodium_base64_VARIANT_ORIGINAL);
  return encoded.data();
}

void appendLittleEndianUint32(std::ostream &output, const std::uint32_t value) {
  for (int offset = 0; offset != 4; ++offset) {
    output.put(static_cast<char>((value >> (offset * 8)) & 0xff));
  }
}

bool writeLegacyV1EncryptedBackup(const std::string &zip_path,
                                  const std::string &output_path,
                                  const std::string_view recovery_password,
                                  const pcm::backup::MasterKey &master_key) {
  if (sodium_init() < 0) {
    return false;
  }
  std::ifstream input(zip_path, std::ios::binary);
  if (!input) {
    return false;
  }

  std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> wrapNonce{};
  std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES> streamHeader{};
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> wrappingKey{};
  std::array<unsigned char,
             crypto_aead_xchacha20poly1305_ietf_ABYTES + pcm::backup::MasterKey{}.bytes.size()>
      wrappedMasterKey{};
  randombytes_buf(salt.data(), salt.size());
  randombytes_buf(wrapNonce.data(), wrapNonce.size());

  LegacyContainerHeaderFixture header;
  header.salt = base64Encode(salt);
  header.wrap_nonce = base64Encode(wrapNonce);
  if (crypto_pwhash(wrappingKey.data(), wrappingKey.size(), recovery_password.data(),
                    recovery_password.size(), salt.data(), header.kdf_opslimit,
                    header.kdf_memlimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    return false;
  }

  crypto_secretstream_xchacha20poly1305_state streamState{};
  if (crypto_secretstream_xchacha20poly1305_init_push(&streamState, streamHeader.data(),
                                                       master_key.bytes.data()) != 0) {
    return false;
  }
  header.stream_header = base64Encode(streamHeader);
  const auto wrapHeader = LegacyHeaderForWrapFixture{
      .container_version = header.container_version,
      .kdf_algorithm = header.kdf_algorithm,
      .kdf_opslimit = header.kdf_opslimit,
      .kdf_memlimit = header.kdf_memlimit,
      .salt = header.salt,
      .wrap_nonce = header.wrap_nonce,
      .stream_header = header.stream_header,
      .chunk_size = header.chunk_size};
  const auto wrapAdditionalData = std::string{kLegacyContainerMagic} +
                                  rfl::json::write(wrapHeader);
  unsigned long long wrappedSize = 0;
  if (crypto_aead_xchacha20poly1305_ietf_encrypt(
          wrappedMasterKey.data(), &wrappedSize, master_key.bytes.data(), master_key.bytes.size(),
          reinterpret_cast<const unsigned char *>(wrapAdditionalData.data()),
          wrapAdditionalData.size(), nullptr, wrapNonce.data(), wrappingKey.data()) != 0 ||
      wrappedSize != wrappedMasterKey.size()) {
    return false;
  }
  header.wrapped_master_key = base64Encode(wrappedMasterKey);
  const auto serializedHeader = rfl::json::write(header);

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(kLegacyContainerMagic.data(), kLegacyContainerMagic.size());
  appendLittleEndianUint32(output, static_cast<std::uint32_t>(serializedHeader.size()));
  output.write(serializedHeader.data(), serializedHeader.size());

  std::vector<unsigned char> plaintext(header.chunk_size);
  std::vector<unsigned char> ciphertext(header.chunk_size +
                                         crypto_secretstream_xchacha20poly1305_ABYTES);
  while (output) {
    input.read(reinterpret_cast<char *>(plaintext.data()), plaintext.size());
    const auto plainSize = static_cast<unsigned long long>(input.gcount());
    if (input.bad() || (!input.eof() && input.fail())) {
      return false;
    }
    const auto tag = input.eof() ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                                 : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
    unsigned long long ciphertextSize = 0;
    if (crypto_secretstream_xchacha20poly1305_push(
            &streamState, ciphertext.data(), &ciphertextSize, plaintext.data(), plainSize,
            reinterpret_cast<const unsigned char *>(serializedHeader.data()),
            serializedHeader.size(), tag) != 0) {
      return false;
    }
    appendLittleEndianUint32(output, static_cast<std::uint32_t>(ciphertextSize));
    output.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertextSize);
    if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
      break;
    }
  }
  sodium_memzero(wrappingKey.data(), wrappingKey.size());
  sodium_memzero(wrappedMasterKey.data(), wrappedMasterKey.size());
  return static_cast<bool>(output);
}

QCoreApplication &testApplication() {
  if (auto *application = QCoreApplication::instance(); application != nullptr) {
    return *application;
  }
  static int argc = 1;
  static char applicationName[] = "Sessio_backup_tests";
  static char *argv[] = {applicationName, nullptr};
  static QCoreApplication application(argc, argv);
  return application;
}

class InMemoryCredentialStore final : public pcm::backup::CredentialStore {
public:
  bool available = true;
  int readCalls = 0;
  pcm::backup::MasterKey masterKey = fixedMasterKey();

  void readWorkspaceMasterKey(const QString &) override {
    ++readCalls;
    emit readFinished(available, masterKey,
                      available ? QString{} : QStringLiteral("system keychain unavailable"));
  }

  void writeWorkspaceMasterKey(const QString &, const pcm::backup::MasterKey &key) override {
    masterKey = key;
    emit writeFinished(true, {});
  }
};

class BackupSettingsGuard final {
public:
  BackupSettingsGuard()
      : mAutoBackupEnabled(pcm::app_settings::autoBackupEnabled()),
        mLastRunAtMs(pcm::app_settings::autoBackupLastRunAtMs()),
        mDestination(pcm::app_settings::autoBackupDestination()),
        mEncryptionEnabled(pcm::app_settings::backupEncryptionEnabled()),
        mKeychainEntry(pcm::app_settings::backupEncryptionKeychainEntry()),
        mRecoveryEnvelope(pcm::app_settings::backupEncryptionRecoveryEnvelope()) {}

  ~BackupSettingsGuard() {
    pcm::app_settings::setAutoBackupEnabled(mAutoBackupEnabled);
    pcm::app_settings::setAutoBackupLastRunAtMs(mLastRunAtMs);
    pcm::app_settings::setAutoBackupDestination(mDestination);
    pcm::app_settings::setBackupEncryptionEnabled(mEncryptionEnabled);
    pcm::app_settings::setBackupEncryptionKeychainEntry(mKeychainEntry);
    pcm::app_settings::setBackupEncryptionRecoveryEnvelope(mRecoveryEnvelope);
  }

private:
  bool mAutoBackupEnabled;
  qint64 mLastRunAtMs;
  QString mDestination;
  bool mEncryptionEnabled;
  QString mKeychainEntry;
  QString mRecoveryEnvelope;
};

} // namespace

TEST(EncryptedContainerTest, RoundTripsZipWithRecoveryPassword) {
  const auto plain = writeTempFile("tmp_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_encrypted.psybackup");
  const auto restored = tempPath("tmp_restored.zip");
  const auto key = fixedMasterKey();

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  EXPECT_EQ(pcm::backup::detect_backup_container(encrypted),
            pcm::backup::BackupContainerKind::Encrypted);
  ASSERT_TRUE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                testRecoveryPhrase())
                  .ok);
  EXPECT_EQ(readFile(restored), readFile(plain));

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
  Poco::File(restored).remove();
}

TEST(EncryptedContainerTest, ReusesRecoveryEnvelopeAcrossIndependentBackups) {
  const auto firstPlain = writeTempFile("tmp_envelope_first.zip", "PK\x03\x04first");
  const auto secondPlain = writeTempFile("tmp_envelope_second.zip", "PK\x03\x04second");
  const auto firstEncrypted = tempPath("tmp_envelope_first.psybackup");
  const auto secondEncrypted = tempPath("tmp_envelope_second.psybackup");
  const auto firstRestored = tempPath("tmp_envelope_first_restored.zip");
  const auto secondRestored = tempPath("tmp_envelope_second_restored.zip");
  const auto envelope = recoveryEnvelope();
  removeIfExists(firstRestored);
  removeIfExists(secondRestored);

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(firstPlain, firstEncrypted, envelope,
                                                fixedMasterKey())
                  .ok);
  ASSERT_TRUE(pcm::backup::encrypt_backup_file(secondPlain, secondEncrypted, envelope,
                                                fixedMasterKey())
                  .ok);
  ASSERT_TRUE(pcm::backup::decrypt_backup_file(firstEncrypted, firstRestored,
                                                testRecoveryPhrase())
                  .ok);
  ASSERT_TRUE(pcm::backup::decrypt_backup_file(secondEncrypted, secondRestored,
                                                testRecoveryPhrase())
                  .ok);
  EXPECT_EQ(readFile(firstRestored), readFile(firstPlain));
  EXPECT_EQ(readFile(secondRestored), readFile(secondPlain));

  Poco::File(firstPlain).remove();
  Poco::File(secondPlain).remove();
  Poco::File(firstEncrypted).remove();
  Poco::File(secondEncrypted).remove();
  Poco::File(firstRestored).remove();
  Poco::File(secondRestored).remove();
}

TEST(EncryptedContainerTest, RejectsMalformedRecoveryEnvelopeWithoutWritingOutput) {
  const auto plain = writeTempFile("tmp_bad_envelope.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_bad_envelope.psybackup");
  removeIfExists(encrypted);
  pcm::backup::RecoveryEnvelope malformed;

  EXPECT_FALSE(pcm::backup::encrypt_backup_file(plain, encrypted, malformed,
                                                 fixedMasterKey())
                   .ok);
  EXPECT_FALSE(Poco::File(encrypted).exists());

  Poco::File(plain).remove();
}

TEST(EncryptedContainerTest, RejectsWrongPasswordWithoutWritingPlaintext) {
  const auto plain = writeTempFile("tmp_wrong_password_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_wrong_password.psybackup");
  const auto restored = tempPath("tmp_wrong_password_restored.zip");
  const auto key = fixedMasterKey();
  removeIfExists(restored);

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  EXPECT_FALSE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                 otherRecoveryPhrase())
                   .ok);
  EXPECT_FALSE(Poco::File(restored).exists());

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
}

TEST(EncryptedContainerTest, RejectsCiphertextTampering) {
  const auto plain = writeTempFile("tmp_tampered_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_tampered.psybackup");
  const auto restored = tempPath("tmp_tampered_restored.zip");
  const auto key = fixedMasterKey();
  removeIfExists(restored);

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  std::fstream ciphertext(encrypted, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(ciphertext);
  ciphertext.seekg(-1, std::ios::end);
  char byte = 0;
  ciphertext.read(&byte, 1);
  ciphertext.seekp(-1, std::ios::end);
  byte ^= 0x01;
  ciphertext.write(&byte, 1);
  ciphertext.close();

  EXPECT_FALSE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                 testRecoveryPhrase())
                   .ok);
  EXPECT_FALSE(Poco::File(restored).exists());

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
}

TEST(EncryptedContainerTest, KeepsPlainZipDetectionCompatible) {
  const auto plain = writeTempFile("tmp_plain_detection.zip", "PK\x03\x04payload");

  EXPECT_EQ(pcm::backup::detect_backup_container(plain),
            pcm::backup::BackupContainerKind::PlainZip);

  Poco::File(plain).remove();
}

TEST(EncryptedContainerTest, PreservesExistingOutputWhenPublishingFails) {
  const auto plain = writeTempFile("tmp_publish_plain.zip", "PK\x03\x04payload");
  const auto outputDirectory = tempPath("tmp_publish_output");
  const auto key = fixedMasterKey();
  Poco::File output(outputDirectory);
  if (output.exists()) {
    output.remove(true);
  }
  output.createDirectory();

  EXPECT_FALSE(pcm::backup::encrypt_backup_file(plain, outputDirectory,
                                                 testRecoveryPhrase(), key)
                   .ok);
  EXPECT_TRUE(output.exists());
  EXPECT_TRUE(output.isDirectory());

  Poco::File(plain).remove();
  output.remove(true);
}

TEST(EncryptedContainerTest, ReplacesExistingRegularOutputFile) {
  const auto plain = writeTempFile("tmp_replace_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_replace.psybackup");
  const auto restored = writeTempFile("tmp_replace_restored.zip", "old plaintext");
  const auto key = fixedMasterKey();

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  ASSERT_TRUE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                testRecoveryPhrase())
                  .ok);
  EXPECT_EQ(readFile(restored), readFile(plain));

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
  Poco::File(restored).remove();
}

TEST(EncryptedContainerTest, RejectsOversizedHeaderWithoutWritingPlaintext) {
  const auto encrypted = tempPath("tmp_oversized_header.psybackup");
  const auto restored = tempPath("tmp_oversized_header_restored.zip");
  removeIfExists(restored);
  overwriteFile(encrypted, "PCMENC01\x01\x40\x00\x00");

  EXPECT_FALSE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                 testRecoveryPhrase())
                   .ok);
  EXPECT_FALSE(Poco::File(restored).exists());

  Poco::File(encrypted).remove();
}

TEST(EncryptedContainerTest, RejectsUnsupportedVersionWithoutWritingPlaintext) {
  const auto plain = writeTempFile("tmp_version_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_version.psybackup");
  const auto restored = tempPath("tmp_version_restored.zip");
  const auto key = fixedMasterKey();
  removeIfExists(restored);

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  auto contents = readFile(encrypted);
  const auto version = contents.find("\"container_version\":2");
  ASSERT_NE(version, std::string::npos);
  contents[version + std::string{"\"container_version\":"}.size()] = '3';
  overwriteFile(encrypted, contents);

  EXPECT_FALSE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                 testRecoveryPhrase())
                   .ok);
  EXPECT_FALSE(Poco::File(restored).exists());

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
}

TEST(EncryptedContainerTest, RejectsTruncatedFinalRecordWithoutWritingPlaintext) {
  const auto plain = writeTempFile("tmp_truncated_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_truncated.psybackup");
  const auto restored = tempPath("tmp_truncated_restored.zip");
  const auto key = fixedMasterKey();
  removeIfExists(restored);

  ASSERT_TRUE(pcm::backup::encrypt_backup_file(plain, encrypted,
                                                testRecoveryPhrase(), key)
                  .ok);
  auto contents = readFile(encrypted);
  ASSERT_FALSE(contents.empty());
  contents.pop_back();
  overwriteFile(encrypted, contents);

  EXPECT_FALSE(pcm::backup::decrypt_backup_file(encrypted, restored,
                                                 testRecoveryPhrase())
                   .ok);
  EXPECT_FALSE(Poco::File(restored).exists());

  Poco::File(plain).remove();
  Poco::File(encrypted).remove();
}

TEST(EncryptedContainerTest, RejectsTooShortPasswordWithoutWritingOutput) {
  const auto plain = writeTempFile("tmp_short_password_plain.zip", "PK\x03\x04payload");
  const auto encrypted = tempPath("tmp_short_password.psybackup");
  const auto key = fixedMasterKey();
  removeIfExists(encrypted);

  EXPECT_FALSE(pcm::backup::encrypt_backup_file(plain, encrypted, "too short", key).ok);
  EXPECT_FALSE(Poco::File(encrypted).exists());

  Poco::File(plain).remove();
}

TEST(ChecksumUtilsTest, Sha256FileMatchesKnownVectors) {
  const auto helloPath = writeTempFile("tmp_sha256_hello.txt", "hello world");
  EXPECT_EQ(pcm::backup::sha256_file(helloPath),
           "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
  Poco::File(helloPath).remove();

  const auto emptyPath = writeTempFile("tmp_sha256_empty.txt", "");
  EXPECT_EQ(pcm::backup::sha256_file(emptyPath),
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  Poco::File(emptyPath).remove();
}

TEST(ChecksumUtilsTest, Sha256FileThrowsForMissingFile) {
  const auto missingPath =
      Poco::Path(Poco::Path::current()).append("tmp_does_not_exist.txt").toString();
  EXPECT_THROW(pcm::backup::sha256_file(missingPath), std::runtime_error);
}

TEST(BackupManifestTest, JsonRoundTripPreservesAllFields) {
  pcm::backup::BackupManifest manifest;
  manifest.psybackup_format_version = 1;
  manifest.created_at = 1732550400000;
  manifest.workspace_uuid = "11111111-2222-3333-4444-555555555555";
  manifest.schema_version = 1;
  manifest.backup_format_version = 1;
  manifest.kind = "database";
  manifest.entries.push_back(
      pcm::backup::BackupEntry{"database/Client.parquet", 4821,
                               "0123456789abcdef0123456789abcdef"});

  const auto json = rfl::json::write(manifest, rfl::json::pretty);
  const auto parsed = rfl::json::read<pcm::backup::BackupManifest>(json);
  ASSERT_TRUE(parsed);

  EXPECT_EQ(parsed.value().psybackup_format_version, 1);
  EXPECT_EQ(parsed.value().created_at, 1732550400000);
  EXPECT_EQ(parsed.value().workspace_uuid,
           "11111111-2222-3333-4444-555555555555");
  EXPECT_EQ(parsed.value().kind, "database");
  ASSERT_EQ(parsed.value().entries.size(), 1u);
  EXPECT_EQ(parsed.value().entries[0].path, "database/Client.parquet");
  EXPECT_EQ(parsed.value().entries[0].size_bytes, 4821);
  EXPECT_EQ(parsed.value().entries[0].sha256,
           "0123456789abcdef0123456789abcdef");
}

namespace {

pcm::database::Database makeTestDatabase(const std::string &dirName) {
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{
          .db_pth = Poco::Path(Poco::Path::current()).append(dirName)}};
  Poco::File dbDir(conf.db_conf().db_pth);
  if (dbDir.exists()) {
    dbDir.remove(true);
  }
  return pcm::database::Database{conf};
}

std::optional<pcm::database::Database> backupAndRestore(pcm::database::Database &sourceDb,
                                                         const std::string &tag) {
  const auto backupPath =
      Poco::Path(Poco::Path::current()).append(tag + ".psybackup").toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  if (!pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok) {
    return std::nullopt;
  }

  const auto targetPath = Poco::Path(Poco::Path::current()).append(tag + "_target").toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }

  const auto restoreResult = pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  if (!restoreResult.ok) {
    return std::nullopt;
  }

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  return pcm::database::Database{targetConfig};
}

pcm::backup::BackupManifest extractManifest(const std::string &backupPath,
                                            const std::string &extractDir) {
  std::ifstream zipIn(backupPath, std::ios::binary);
  Poco::Zip::Decompress decompress(zipIn, Poco::Path(extractDir));
  decompress.decompressAllFiles();
  const auto manifestPath =
      Poco::Path(extractDir).append("manifest.json").toString();
  return rfl::json::load<pcm::backup::BackupManifest>(manifestPath).value();
}

} // namespace

TEST(BackupServiceTest, CreateBackupDatabaseOnlyProducesValidArchive) {
  auto db = makeTestDatabase("tmp_dir_backup_db_only");

  DuckClient client;
  client.name = std::string{"Backup"};
  client.last_name = std::string{"Client"};
  ASSERT_GT(db.add_client(client), 0);

  const auto destPath =
      Poco::Path(Poco::Path::current()).append("tmp_backup_db_only.psybackup")
          .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  const auto result = service.create_backup(db, destPath);
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(Poco::File(destPath).exists());

  const auto extractDir =
      Poco::Path(Poco::Path::current()).append("tmp_backup_db_only_extract")
          .toString();
  Poco::File extractDirFile(extractDir);
  if (extractDirFile.exists()) {
    extractDirFile.remove(true);
  }

  const auto manifest = extractManifest(destPath, extractDir);
  EXPECT_EQ(manifest.kind, "database");
  ASSERT_FALSE(manifest.entries.empty());

  bool foundClientTable = false;
  for (const auto &entry : manifest.entries) {
    Poco::Path entryPath(extractDir);
    entryPath.append(Poco::Path(entry.path, Poco::Path::PATH_UNIX));
    ASSERT_TRUE(Poco::File(entryPath).exists()) << entry.path;
    EXPECT_EQ(pcm::backup::sha256_file(entryPath.toString()), entry.sha256)
        << entry.path;
    if (entry.path == "database/client.parquet") {
      foundClientTable = true;
    }
  }
  EXPECT_TRUE(foundClientTable);

  const auto meta = db.get_application_metadata();
  EXPECT_EQ(manifest.workspace_uuid, meta.workspace_uuid);
  EXPECT_EQ(manifest.schema_version, meta.schema_version);
  EXPECT_EQ(manifest.backup_format_version, meta.backup_format_version);

  extractDirFile.remove(true);
  destFile.remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_backup_db_only"))
      .remove(true);
}

TEST(BackupServiceTest, EncryptedBackupHidesZipAndValidatesAfterDecryption) {
  auto db = makeTestDatabase("tmp_encrypted_backup_source");
  DuckClient client;
  client.name = std::string{"Encrypted"};
  ASSERT_GT(db.add_client(client), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_encrypted_backup.psybackup")
                              .toString();
  removeIfExists(backupPath);

  pcm::backup::BackupOptions options;
  options.encryption = pcm::backup::BackupEncryptionOptions{
      .master_key = fixedMasterKey(),
      .recovery_password = testRecoveryPhrase()};
  const auto backupResult =
      pcm::backup::BackupService{}.create_backup(db, backupPath, options);
  ASSERT_TRUE(backupResult.ok) << backupResult.error;
  EXPECT_EQ(pcm::backup::detect_backup_container(backupPath),
            pcm::backup::BackupContainerKind::Encrypted);

  const auto validation = pcm::backup::BackupValidator{}.validate(
      backupPath, testRecoveryPhrase());
  EXPECT_TRUE(validation.ok);
  EXPECT_TRUE(validation.errors.empty());

  removeIfExists(backupPath);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_encrypted_backup_source"))
      .remove(true);
}

TEST(BackupServiceTest, RequiresExactlyOneEncryptionRecoveryMethod) {
  auto db = makeTestDatabase("tmp_encryption_recovery_method_source");
  const auto backupPath = tempPath("tmp_encryption_recovery_method.psybackup");
  removeIfExists(backupPath);

  pcm::backup::BackupOptions missingRecoveryMethod;
  missingRecoveryMethod.encryption = pcm::backup::BackupEncryptionOptions{
      .master_key = fixedMasterKey()};
  const auto missingResult =
      pcm::backup::BackupService{}.create_backup(db, backupPath, missingRecoveryMethod);
  EXPECT_FALSE(missingResult.ok);
  EXPECT_FALSE(Poco::File(backupPath).exists());

  pcm::backup::BackupOptions ambiguousRecoveryMethod;
  ambiguousRecoveryMethod.encryption = pcm::backup::BackupEncryptionOptions{
      .master_key = fixedMasterKey(),
      .recovery_password = testRecoveryPhrase(),
      .recovery_envelope = recoveryEnvelope(),
  };
  const auto ambiguousResult =
      pcm::backup::BackupService{}.create_backup(db, backupPath, ambiguousRecoveryMethod);
  EXPECT_FALSE(ambiguousResult.ok);
  EXPECT_FALSE(Poco::File(backupPath).exists());

  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_encryption_recovery_method_source"))
      .remove(true);
}

TEST(BackupServiceTest, CreateBackupWithAttachmentsIncludesAttachmentTree) {
  auto db = makeTestDatabase("tmp_dir_backup_with_attachments");

  const auto attachmentsRoot =
      Poco::Path(Poco::Path::current()).append("tmp_attachments_root").toString();
  Poco::File attachmentsRootFile(attachmentsRoot);
  if (attachmentsRootFile.exists()) {
    attachmentsRootFile.remove(true);
  }
  Poco::File(Poco::Path(attachmentsRoot).append("42").append("7")).createDirectories();
  std::ofstream attachmentOut(
      Poco::Path(attachmentsRoot).append("42").append("7").append("note.txt")
          .toString(),
      std::ios::binary | std::ios::trunc);
  attachmentOut << "attachment contents";
  attachmentOut.close();

  const auto destPath =
      Poco::Path(Poco::Path::current()).append("tmp_backup_with_attachments.psybackup")
          .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  pcm::backup::BackupOptions options;
  options.attachments_root = attachmentsRoot;
  const auto result = service.create_backup(db, destPath, options);
  ASSERT_TRUE(result.ok) << result.error;

  const auto extractDir =
      Poco::Path(Poco::Path::current())
          .append("tmp_backup_with_attachments_extract")
          .toString();
  Poco::File extractDirFile(extractDir);
  if (extractDirFile.exists()) {
    extractDirFile.remove(true);
  }

  const auto manifest = extractManifest(destPath, extractDir);
  EXPECT_EQ(manifest.kind, "database_and_attachments");

  bool foundAttachment = false;
  for (const auto &entry : manifest.entries) {
    Poco::Path entryPath(extractDir);
    entryPath.append(Poco::Path(entry.path, Poco::Path::PATH_UNIX));
    ASSERT_TRUE(Poco::File(entryPath).exists()) << entry.path;
    EXPECT_EQ(pcm::backup::sha256_file(entryPath.toString()), entry.sha256)
        << entry.path;
    if (entry.path == "attachments/42/7/note.txt") {
      foundAttachment = true;
    }
  }
  EXPECT_TRUE(foundAttachment);

  extractDirFile.remove(true);
  destFile.remove();
  attachmentsRootFile.remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                .append("tmp_dir_backup_with_attachments"))
      .remove(true);
}

TEST(BackupServiceTest, CreateBackupFailsWhenAttachmentsRootMissing) {
  auto db = makeTestDatabase("tmp_dir_backup_missing_attachments");

  const auto destPath =
      Poco::Path(Poco::Path::current())
          .append("tmp_backup_missing_attachments.psybackup")
          .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  pcm::backup::BackupOptions options;
  options.attachments_root = Poco::Path(Poco::Path::current())
                                 .append("tmp_attachments_does_not_exist")
                                 .toString();
  const auto result = service.create_backup(db, destPath, options);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
  EXPECT_FALSE(Poco::File(destPath).exists());

  Poco::File(Poco::Path(Poco::Path::current())
                .append("tmp_dir_backup_missing_attachments"))
      .remove(true);
}

TEST(BackupValidatorTest, ValidatesACleanBackupAsOk) {
  auto db = makeTestDatabase("tmp_dir_validate_ok");

  DuckClient client;
  client.name = std::string{"Validate"};
  client.last_name = std::string{"Ok"};
  ASSERT_GT(db.add_client(client), 0);

  const auto destPath = Poco::Path(Poco::Path::current())
                            .append("tmp_backup_validate_ok.psybackup")
                            .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  ASSERT_TRUE(service.create_backup(db, destPath).ok);

  pcm::backup::BackupValidator validator;
  const auto result = validator.validate(destPath);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.errors.empty());

  destFile.remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_validate_ok"))
      .remove(true);
}

TEST(BackupValidatorTest, DetectsCorruptedEntry) {
  auto db = makeTestDatabase("tmp_dir_validate_corrupt");

  DuckClient client;
  client.name = std::string{"Validate"};
  client.last_name = std::string{"Corrupt"};
  ASSERT_GT(db.add_client(client), 0);

  const auto destPath = Poco::Path(Poco::Path::current())
                            .append("tmp_backup_validate_corrupt.psybackup")
                            .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  ASSERT_TRUE(service.create_backup(db, destPath).ok);

  {
    std::fstream f(destPath, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(f);
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    ASSERT_GT(size, 100);
    const std::streamoff mid = size / 2;
    f.seekg(mid, std::ios::beg);
    char original = 0;
    f.read(&original, 1);
    const char corrupted = static_cast<char>(~original);
    f.seekp(mid, std::ios::beg);
    f.write(&corrupted, 1);
  }

  pcm::backup::BackupValidator validator;
  const auto result = validator.validate(destPath);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());

  destFile.remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_validate_corrupt"))
      .remove(true);
}

TEST(BackupValidatorTest, RejectsUnsupportedFormatVersion) {
  auto db = makeTestDatabase("tmp_dir_validate_version");

  const auto destPath = Poco::Path(Poco::Path::current())
                            .append("tmp_backup_validate_version.psybackup")
                            .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  ASSERT_TRUE(service.create_backup(db, destPath).ok);

  const auto extractDir = Poco::Path(Poco::Path::current())
                              .append("tmp_backup_validate_version_extract")
                              .toString();
  Poco::File extractDirFile(extractDir);
  if (extractDirFile.exists()) {
    extractDirFile.remove(true);
  }
  auto manifest = extractManifest(destPath, extractDir);
  manifest.psybackup_format_version = 999;
  ASSERT_TRUE(rfl::json::save(
      Poco::Path(extractDir).append("manifest.json").toString(), manifest,
      rfl::json::pretty));

  const auto tamperedPath = Poco::Path(Poco::Path::current())
                                .append("tmp_backup_validate_version_tampered.psybackup")
                                .toString();
  Poco::File tamperedFile(tamperedPath);
  if (tamperedFile.exists()) {
    tamperedFile.remove();
  }
  {
    std::ofstream zipOut(tamperedPath, std::ios::binary | std::ios::trunc);
    Poco::Zip::Compress compress(zipOut, true);
    compress.addRecursive(Poco::Path(extractDir),
                          Poco::Zip::ZipCommon::CL_MAXIMUM, true);
    compress.close();
  }

  pcm::backup::BackupValidator validator;
  const auto result = validator.validate(tamperedPath);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());

  manifest.psybackup_format_version = 1;
  manifest.schema_version = 3;
  ASSERT_TRUE(rfl::json::save(
      Poco::Path(extractDir).append("manifest.json").toString(), manifest, rfl::json::pretty));
  {
    std::ofstream zipOut(tamperedPath, std::ios::binary | std::ios::trunc);
    Poco::Zip::Compress compress(zipOut, true);
    compress.addRecursive(Poco::Path(extractDir), Poco::Zip::ZipCommon::CL_MAXIMUM, true);
    compress.close();
  }
  EXPECT_FALSE(validator.validate(tamperedPath).ok);
  const auto futureTarget = extractDir + "-future-target";
  EXPECT_FALSE(pcm::backup::RestoreService{}.restore_backup(tamperedPath, futureTarget).ok);
  EXPECT_FALSE(Poco::File(futureTarget).exists());

  extractDirFile.remove(true);
  destFile.remove();
  tamperedFile.remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_validate_version"))
      .remove(true);
}

TEST(BackupServiceTest, FailedBackupLeavesExistingDestinationUntouched) {
  auto db = makeTestDatabase("tmp_dir_atomicity");

  DuckClient client;
  client.name = std::string{"Atomic"};
  client.last_name = std::string{"Ity"};
  ASSERT_GT(db.add_client(client), 0);

  const auto destPath = Poco::Path(Poco::Path::current())
                            .append("tmp_backup_atomicity.psybackup")
                            .toString();
  Poco::File destFile(destPath);
  if (destFile.exists()) {
    destFile.remove();
  }

  pcm::backup::BackupService service;
  ASSERT_TRUE(service.create_backup(db, destPath).ok);

  std::ifstream before(destPath, std::ios::binary);
  const std::string beforeContents((std::istreambuf_iterator<char>(before)),
                                   std::istreambuf_iterator<char>());
  before.close();

  pcm::backup::BackupValidator validator;
  ASSERT_TRUE(validator.validate(destPath).ok);

  pcm::backup::BackupOptions options;
  options.attachments_root = Poco::Path(Poco::Path::current())
                                 .append("tmp_atomicity_missing_attachments")
                                 .toString();
  const auto failedResult = service.create_backup(db, destPath, options);
  EXPECT_FALSE(failedResult.ok);

  std::ifstream after(destPath, std::ios::binary);
  const std::string afterContents((std::istreambuf_iterator<char>(after)),
                                  std::istreambuf_iterator<char>());
  after.close();

  EXPECT_EQ(beforeContents, afterContents);
  EXPECT_TRUE(validator.validate(destPath).ok);

  const auto parentDir = Poco::Path(Poco::Path::current());
  Poco::DirectoryIterator dirIt(parentDir);
  const Poco::DirectoryIterator dirEnd;
  for (; dirIt != dirEnd; ++dirIt) {
    EXPECT_EQ(dirIt.name().find("tmp_backup_atomicity.psybackup.partial-"),
             std::string::npos)
        << "leftover partial file: " << dirIt.name();
  }

  destFile.remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_atomicity"))
      .remove(true);
}

TEST(BackupServiceTest, FailedRenameLeavesNoPartialFile) {
  auto db = makeTestDatabase("tmp_dir_failed_rename");

  const auto destPath = Poco::Path(Poco::Path::current())
                            .append("tmp_backup_failed_rename.psybackup")
                            .toString();
  Poco::File destBlocker(destPath);
  if (destBlocker.exists()) {
    destBlocker.remove(true);
  }
  destBlocker.createDirectories();

  pcm::backup::BackupService service;
  const auto result = service.create_backup(db, destPath);
  EXPECT_FALSE(result.ok);

  Poco::File destAfter(destPath);
  EXPECT_TRUE(destAfter.exists());
  EXPECT_TRUE(destAfter.isDirectory());

  const auto parentDir = Poco::Path(Poco::Path::current());
  Poco::DirectoryIterator dirIt(parentDir);
  const Poco::DirectoryIterator dirEnd;
  for (; dirIt != dirEnd; ++dirIt) {
    EXPECT_EQ(dirIt.name().find("tmp_backup_failed_rename.psybackup.partial-"),
             std::string::npos)
        << "leftover partial file: " << dirIt.name();
  }

  destBlocker.remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_dir_failed_rename"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresDatabaseSnapshotIntoNewDirectory) {
  auto sourceDb = makeTestDatabase("tmp_restore_source");
  DuckClient client;
  client.name = std::string{"Restored"};
  client.last_name = std::string{"Client"};
  ASSERT_GT(sourceDb.add_client(client), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_database.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;
  EXPECT_TRUE(restoreResult.protective_database_path.empty());

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto clients = restoredDb.get_clients();
  ASSERT_TRUE(std::any_of(
      clients.begin(), clients.end(), [](const auto &storedClient) {
        return storedClient && storedClient->name.value_or("") == "Restored";
      }));

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresTranscriptsAndPhrases) {
  auto sourceDb = makeTestDatabase("tmp_restore_transcript_source");

  DuckEvent event;
  event.name = std::string{"Recorded session"};
  event.start_date = 1730000000000;
  event.end_date = 1730003600000;
  event.duration = 3600;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  const auto eventId = sourceDb.add_event(event);
  ASSERT_GT(eventId, 0);

  const auto transcriptId = sourceDb.add_transcript(
      eventId, "live_local_v1", std::string{"gigaam-v3-rnnt"}, 1730000100000);
  ASSERT_GT(transcriptId, 0);
  DuckTranscriptPhrase phrase;
  phrase.transcript_id = transcriptId;
  phrase.track_role = "participant";
  phrase.speaker_name = std::string{"Client"};
  phrase.start_ms = 1500;
  phrase.end_ms = 4200;
  phrase.text = "Привет, это тест восстановления";
  const auto editedId = sourceDb.add_transcript_phrase(phrase);
  ASSERT_GT(editedId, 0);
  DuckTranscriptPhrase second = phrase;
  second.start_ms = 5000;
  second.end_ms = 6000;
  second.text = "Вторая фраза";
  ASSERT_GT(sourceDb.add_transcript_phrase(second), 0);
  ASSERT_TRUE(sourceDb.update_transcript_phrase_text(editedId, "Привет, это тест восстановления"));
  ASSERT_TRUE(sourceDb.set_transcript_status(transcriptId, "draft"));
  ASSERT_TRUE(sourceDb.revoke_transcript_consent(transcriptId, 1730000300000));
  const auto sourceTranscript = sourceDb.get_transcript(transcriptId);
  ASSERT_NE(sourceTranscript, nullptr);
  const auto standaloneId = sourceDb.add_transcript(std::nullopt, "standalone_scope",
                                                    std::string("standalone_model"), 1730000200000);
  DuckTranscriptPhrase standalonePhrase = phrase;
  standalonePhrase.transcript_id = standaloneId;
  standalonePhrase.text = "Standalone text";
  ASSERT_GT(sourceDb.add_transcript_phrase(standalonePhrase), 0);
  ASSERT_TRUE(sourceDb.set_transcript_status(standaloneId, "draft"));
  DuckClient attached; attached.name = "Existing client";
  const auto attachedId = sourceDb.add_client(attached);
  ASSERT_TRUE(sourceDb.set_transcript_clients(standaloneId, {attachedId}));

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_transcript.psybackup")
                              .toString();
  removeIfExists(backupPath);
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_transcript_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }
  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto restored = restoredDb.get_transcripts_for_event(eventId);
  ASSERT_EQ(restored.size(), 1u);
  EXPECT_EQ(restored[0].status, "draft");
  EXPECT_EQ(restored[0].consent_scope, "live_local_v1");
  EXPECT_EQ(restored[0].model_id.value_or(""), "gigaam-v3-rnnt");
  EXPECT_EQ(restored[0].consent_given_at, 1730000100000);
  ASSERT_TRUE(restored[0].consent_revoked_at.has_value());
  EXPECT_EQ(*restored[0].consent_revoked_at, 1730000300000);
  const auto phrases = restoredDb.get_transcript_phrases(restored[0].id);
  EXPECT_EQ(restored[0].created_at, sourceTranscript->created_at);
  EXPECT_EQ(restored[0].updated_at, sourceTranscript->updated_at);
  ASSERT_EQ(phrases.size(), 2u);
  EXPECT_TRUE(phrases[0].edited);
  EXPECT_FALSE(phrases[1].edited);
  EXPECT_EQ(phrases[0].text, "Привет, это тест восстановления");
  EXPECT_EQ(phrases[0].start_ms, 1500);
  EXPECT_EQ(phrases[0].end_ms, 4200);
  EXPECT_EQ(phrases[0].speaker_name.value_or(""), "Client");
  const auto standalone = restoredDb.get_transcript(standaloneId);
  ASSERT_NE(standalone, nullptr);
  EXPECT_FALSE(standalone->event_id.has_value());
  EXPECT_EQ(standalone->consent_scope, "standalone_scope");
  EXPECT_EQ(standalone->model_id.value_or(""), "standalone_model");
  EXPECT_EQ(restoredDb.get_transcript_client_ids(standaloneId), std::vector<int64_t>{attachedId});
  ASSERT_EQ(restoredDb.get_transcript_phrases(standaloneId).size(), 1);
  EXPECT_EQ(restoredDb.get_transcript_phrases(standaloneId)[0].text, "Standalone text");

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_transcript_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresLegacySchemaOneAndBackfillsTranscriptClientsBeforeReplacement) {
  const auto root = Poco::Path(Poco::Path::current()).append("tmp_legacy_transcript_source").toString();
  auto source = makeTestDatabase("tmp_legacy_transcript_source");
  DuckClient client; client.name = "Legacy client";
  const auto clientId = source.add_client(client);
  DuckEvent event; event.name = "Legacy event"; event.start_date = 1730000000000;
  event.end_date = 1730003600000; event.duration = 3600; event.event_stat_id = 1; event.payment_stat_id = 1;
  const auto eventId = source.add_event(event);
  ASSERT_GT(source.add_event_client(eventId, clientId), 0);
  const auto id = source.add_transcript(eventId, "legacy_scope", std::string("legacy_model"), 1730000100000);
  DuckTranscriptPhrase phrase; phrase.transcript_id = id; phrase.track_role = "participant";
  phrase.text = "Legacy phrase"; phrase.end_ms = 100;
  const auto phraseId = source.add_transcript_phrase(phrase);
  ASSERT_GT(phraseId, 0);
  ASSERT_TRUE(source.set_transcript_status(id, "draft"));
  const auto before = *source.get_transcript(id);
  {
    duckdb::DuckDB raw(root + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE TranscriptClient; "
                           "ALTER TABLE Transcript ALTER COLUMN event_id SET NOT NULL; "
                           "UPDATE ApplicationMetadata SET schema_version=1")->HasError());
  }
  const auto backup = root + ".psybackup";
  removeIfExists(backup);
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(source, backup).ok);
  ASSERT_TRUE(pcm::backup::BackupValidator{}.validate(backup).ok);
  const auto target = root + "-target";
  if (Poco::File(target).exists()) Poco::File(target).remove(true);
  const auto result = pcm::backup::RestoreService{}.restore_backup(backup, target);
  ASSERT_TRUE(result.ok) << result.error;
  {
    // Restore migrates the staged database before moving it into place.
    duckdb::DuckDB raw(target + "/database.db"); duckdb::Connection conn(raw);
    auto version = conn.Query("SELECT schema_version FROM ApplicationMetadata");
    ASSERT_FALSE(version->HasError());
    EXPECT_EQ(version->Fetch()->GetValue(0, 0).GetValue<int32_t>(), 2);
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(target)}};
  {
    pcm::database::Database restored(conf);
    const auto row = restored.get_transcript(id);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->event_id, eventId); EXPECT_EQ(row->consent_scope, before.consent_scope);
    EXPECT_EQ(row->model_id, before.model_id); EXPECT_EQ(row->created_at, before.created_at);
    EXPECT_EQ(row->updated_at, before.updated_at); EXPECT_EQ(row->consent_given_at, before.consent_given_at);
    EXPECT_EQ(restored.get_transcript_client_ids(id), std::vector<int64_t>{clientId});
    ASSERT_EQ(restored.get_transcript_phrases(id).size(), 1);
    EXPECT_EQ(restored.get_transcript_phrases(id)[0].id, phraseId);
    EXPECT_EQ(restored.get_transcript_phrases(id)[0].text, "Legacy phrase");
  }
  Poco::File(backup).remove(); Poco::File(target).remove(true); Poco::File(root).remove(true);
}

TEST(RestoreServiceTest, RestoresEncryptedBackupWithCorrectPassword) {
  auto sourceDb = makeTestDatabase("tmp_restore_encrypted_source");
  DuckClient client;
  client.name = std::string{"Encrypted Restore"};
  ASSERT_GT(sourceDb.add_client(client), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_encrypted.psybackup")
                              .toString();
  removeIfExists(backupPath);
  ASSERT_TRUE(createEncryptedBackup(sourceDb, backupPath));

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_encrypted_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }
  const auto restoreResult = restoreWithRecoveryPhrase(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto clients = restoredDb.get_clients();
  EXPECT_TRUE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Encrypted Restore";
  }));

  removeIfExists(backupPath);
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_restore_encrypted_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresLegacyV1EncryptedBackupWithRecoveryPassword) {
  auto sourceDb = makeTestDatabase("tmp_restore_legacy_encrypted_source");
  DuckClient client;
  client.name = std::string{"Legacy Encrypted Restore"};
  ASSERT_GT(sourceDb.add_client(client), 0);

  const auto plainBackupPath = Poco::Path(Poco::Path::current())
                                   .append("tmp_restore_legacy_plain.psybackup")
                                   .toString();
  const auto encryptedBackupPath = Poco::Path(Poco::Path::current())
                                       .append("tmp_restore_legacy_encrypted.psybackup")
                                       .toString();
  removeIfExists(plainBackupPath);
  removeIfExists(encryptedBackupPath);
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, plainBackupPath).ok);
  ASSERT_TRUE(writeLegacyV1EncryptedBackup(
      plainBackupPath, encryptedBackupPath, testRecoveryPhrase(), fixedMasterKey()));

  const auto validation = pcm::backup::BackupValidator{}.validate(
      encryptedBackupPath, testRecoveryPhrase());
  ASSERT_TRUE(validation.ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_legacy_encrypted_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }
  pcm::backup::RestoreOptions restoreOptions;
  restoreOptions.recovery_password = testRecoveryPhrase();
  const auto restoreResult = pcm::backup::RestoreService{}.restore_backup(
      encryptedBackupPath, targetPath, restoreOptions);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto clients = restoredDb.get_clients();
  EXPECT_TRUE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Legacy Encrypted Restore";
  }));

  removeIfExists(plainBackupPath);
  removeIfExists(encryptedBackupPath);
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_restore_legacy_encrypted_source"))
      .remove(true);
}

TEST(RestoreServiceTest, WrongPasswordLeavesExistingDatabaseUntouched) {
  auto sourceDb = makeTestDatabase("tmp_restore_wrong_password_source");
  DuckClient sourceClient;
  sourceClient.name = std::string{"Replacement"};
  ASSERT_GT(sourceDb.add_client(sourceClient), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_wrong_password.psybackup")
                              .toString();
  removeIfExists(backupPath);
  ASSERT_TRUE(createEncryptedBackup(sourceDb, backupPath));

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_wrong_password_target")
                              .toString();
  {
    auto targetDb = makeTestDatabase("tmp_restore_wrong_password_target");
    DuckClient existingClient;
    existingClient.name = std::string{"Unchanged"};
    ASSERT_GT(targetDb.add_client(existingClient), 0);
  }

  pcm::backup::RestoreOptions restoreOptions;
  restoreOptions.recovery_password = otherRecoveryPhrase();
  const auto restoreResult = pcm::backup::RestoreService{}.restore_backup(
      backupPath, targetPath, restoreOptions);
  EXPECT_FALSE(restoreResult.ok);
  EXPECT_EQ(restoreResult.error, "backup validation failed: cannot decrypt backup");

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database unchangedDb{targetConfig};
  const auto clients = unchangedDb.get_clients();
  EXPECT_TRUE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Unchanged";
  }));
  EXPECT_FALSE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Replacement";
  }));

  removeIfExists(backupPath);
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_restore_wrong_password_source"))
      .remove(true);
}

TEST(RestoreServiceTest,
     TamperedEncryptedBackupLeavesExistingDatabaseAndAttachmentsUntouched) {
  auto sourceDb = makeTestDatabase("tmp_restore_tampered_source");
  DuckClient sourceClient;
  sourceClient.name = std::string{"Replacement"};
  ASSERT_GT(sourceDb.add_client(sourceClient), 0);

  const auto sourceAttachments = Poco::Path(Poco::Path::current())
                                     .append("tmp_restore_tampered_source_files")
                                     .toString();
  if (Poco::File(sourceAttachments).exists()) {
    Poco::File(sourceAttachments).remove(true);
  }
  Poco::File(sourceAttachments).createDirectories();
  std::ofstream(Poco::Path(sourceAttachments).append("note.txt").toString())
      << "replacement attachment";

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_tampered.psybackup")
                              .toString();
  removeIfExists(backupPath);
  pcm::backup::BackupOptions backupOptions;
  backupOptions.attachments_root = sourceAttachments;
  backupOptions.encryption = pcm::backup::BackupEncryptionOptions{
      .master_key = fixedMasterKey(),
      .recovery_password = testRecoveryPhrase()};
  ASSERT_TRUE(pcm::backup::BackupService{}
                  .create_backup(sourceDb, backupPath, backupOptions)
                  .ok);

  {
    std::fstream backup(backupPath, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(backup);
    backup.seekg(-1, std::ios::end);
    char byte = 0;
    backup.read(&byte, 1);
    backup.seekp(-1, std::ios::end);
    byte ^= 0x01;
    backup.write(&byte, 1);
  }

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_tampered_target")
                              .toString();
  {
    auto targetDb = makeTestDatabase("tmp_restore_tampered_target");
    DuckClient existingClient;
    existingClient.name = std::string{"Unchanged"};
    ASSERT_GT(targetDb.add_client(existingClient), 0);
  }
  const auto targetAttachments = Poco::Path(Poco::Path::current())
                                     .append("tmp_restore_tampered_target_files")
                                     .toString();
  if (Poco::File(targetAttachments).exists()) {
    Poco::File(targetAttachments).remove(true);
  }
  Poco::File(targetAttachments).createDirectories();
  std::ofstream(Poco::Path(targetAttachments).append("note.txt").toString())
      << "unchanged attachment";

  pcm::backup::RestoreOptions restoreOptions;
  restoreOptions.attachments_root = targetAttachments;
  restoreOptions.recovery_password = testRecoveryPhrase();
  const auto restoreResult = pcm::backup::RestoreService{}.restore_backup(
      backupPath, targetPath, restoreOptions);
  EXPECT_FALSE(restoreResult.ok);
  EXPECT_EQ(restoreResult.error, "backup validation failed: cannot decrypt backup");

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database unchangedDb{targetConfig};
  const auto clients = unchangedDb.get_clients();
  EXPECT_TRUE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Unchanged";
  }));
  EXPECT_FALSE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Replacement";
  }));
  EXPECT_EQ(readFile(Poco::Path(targetAttachments).append("note.txt").toString()),
            "unchanged attachment");

  removeIfExists(backupPath);
  Poco::File(targetPath).remove(true);
  Poco::File(targetAttachments).remove(true);
  Poco::File(sourceAttachments).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_tampered_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresExistingUnencryptedBackupUnchanged) {
  auto sourceDb = makeTestDatabase("tmp_restore_plain_source");
  DuckClient client;
  client.name = std::string{"Plain Restore"};
  ASSERT_GT(sourceDb.add_client(client), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_plain.psybackup")
                              .toString();
  removeIfExists(backupPath);
  ASSERT_TRUE(
      pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_plain_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }
  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto clients = restoredDb.get_clients();
  EXPECT_TRUE(std::any_of(clients.begin(), clients.end(), [](const auto &storedClient) {
    return storedClient && storedClient->name.value_or("") == "Plain Restore";
  }));

  removeIfExists(backupPath);
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_plain_source"))
      .remove(true);
}

TEST(RestoreServiceTest, PreservesCurrentDatabaseBeforeReplacement) {
  auto sourceDb = makeTestDatabase("tmp_restore_replace_source");
  DuckClient sourceClient;
  sourceClient.name = std::string{"New"};
  ASSERT_GT(sourceDb.add_client(sourceClient), 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_replace.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_replace_target")
                              .toString();
  {
    auto targetDb = makeTestDatabase("tmp_restore_replace_target");
    DuckClient oldClient;
    oldClient.name = std::string{"Old"};
    ASSERT_GT(targetDb.add_client(oldClient), 0);
  }

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;
  ASSERT_FALSE(restoreResult.protective_database_path.empty());
  EXPECT_TRUE(Poco::File(restoreResult.protective_database_path).exists());

  pcm::config::Config restoredConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{restoredConfig};
  const auto restoredClients = restoredDb.get_clients();
  ASSERT_TRUE(std::any_of(
      restoredClients.begin(), restoredClients.end(), [](const auto &client) {
        return client && client->name.value_or("") == "New";
      }));

  pcm::config::Config protectiveConfig{.db_conf = pcm::config::DatabaseConfig{
      .db_pth = Poco::Path(restoreResult.protective_database_path)}};
  pcm::database::Database protectiveDb{protectiveConfig};
  const auto oldClients = protectiveDb.get_clients();
  EXPECT_TRUE(std::any_of(
      oldClients.begin(), oldClients.end(), [](const auto &client) {
        return client && client->name.value_or("") == "Old";
      }));

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(restoreResult.protective_database_path).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_restore_replace_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresAttachmentsAlongsideDatabase) {
  auto sourceDb = makeTestDatabase("tmp_restore_attachment_source");
  const auto sourceAttachments = Poco::Path(Poco::Path::current())
                                     .append("tmp_restore_attachment_source_files")
                                     .toString();
  if (Poco::File(sourceAttachments).exists()) {
    Poco::File(sourceAttachments).remove(true);
  }
  Poco::File(sourceAttachments).createDirectories();
  const auto sourceFile = Poco::Path(sourceAttachments).append("note.txt").toString();
  std::ofstream(sourceFile) << "protected note";

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_attachment.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  pcm::backup::BackupOptions backupOptions;
  backupOptions.attachments_root = sourceAttachments;
  ASSERT_TRUE(pcm::backup::BackupService{}
                  .create_backup(sourceDb, backupPath, backupOptions)
                  .ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_attachment_target")
                              .toString();
  const auto targetAttachments = Poco::Path(Poco::Path::current())
                                     .append("tmp_restore_attachment_target_files")
                                     .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }
  if (Poco::File(targetAttachments).exists()) {
    Poco::File(targetAttachments).remove(true);
  }

  pcm::backup::RestoreOptions restoreOptions;
  restoreOptions.attachments_root = targetAttachments;
  const auto restoreResult = pcm::backup::RestoreService{}.restore_backup(
      backupPath, targetPath, restoreOptions);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  const auto restoredFile =
      Poco::Path(targetAttachments).append("note.txt").toString();
  ASSERT_TRUE(Poco::File(restoredFile).exists());
  std::ifstream restoredInput(restoredFile);
  const std::string contents((std::istreambuf_iterator<char>(restoredInput)),
                             std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, "protected note");

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(targetAttachments).remove(true);
  Poco::File(sourceAttachments).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_restore_attachment_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RejectsInvalidBackupWithoutChangingTarget) {
  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_invalid_target")
                              .toString();
  {
    auto targetDb = makeTestDatabase("tmp_restore_invalid_target");
    DuckClient oldClient;
    oldClient.name = std::string{"Safe"};
    ASSERT_GT(targetDb.add_client(oldClient), 0);
  }

  const auto invalidPath = Poco::Path(Poco::Path::current())
                               .append("tmp_invalid_restore.psybackup")
                               .toString();
  if (Poco::File(invalidPath).exists()) {
    Poco::File(invalidPath).remove();
  }
  std::ofstream invalid(invalidPath, std::ios::binary | std::ios::trunc);
  invalid << "not a backup";
  invalid.close();

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(invalidPath, targetPath);
  EXPECT_FALSE(restoreResult.ok);
  EXPECT_TRUE(Poco::File(targetPath).exists());

  Poco::File(invalidPath).remove();
  Poco::File(targetPath).remove(true);
}

TEST(PendingRestoreMarkerTest, WriteThenReadRoundTripsBackupPath) {
  const auto markerPath = Poco::Path(Poco::Path::current())
                              .append("tmp_pending_restore.json")
                              .toString();
  if (Poco::File(markerPath).exists()) {
    Poco::File(markerPath).remove();
  }

  ASSERT_TRUE(pcm::backup::write_pending_restore_marker(
      markerPath, "/backups/example.psybackup"));

  const auto marker = pcm::backup::read_pending_restore_marker(markerPath);
  ASSERT_TRUE(marker.has_value());
  EXPECT_EQ(marker->backup_path, "/backups/example.psybackup");

  Poco::File(markerPath).remove();
}

TEST(PendingRestoreMarkerTest, ReadReturnsNulloptWhenFileIsMissing) {
  const auto markerPath = Poco::Path(Poco::Path::current())
                              .append("tmp_pending_restore_missing.json")
                              .toString();
  if (Poco::File(markerPath).exists()) {
    Poco::File(markerPath).remove();
  }

  EXPECT_FALSE(
      pcm::backup::read_pending_restore_marker(markerPath).has_value());
}

TEST(PendingRestoreMarkerTest, RemoveDeletesTheMarkerFile) {
  const auto markerPath = Poco::Path(Poco::Path::current())
                              .append("tmp_pending_restore_remove.json")
                              .toString();
  ASSERT_TRUE(pcm::backup::write_pending_restore_marker(
      markerPath, "/backups/example.psybackup"));
  ASSERT_TRUE(Poco::File(markerPath).exists());

  pcm::backup::remove_pending_restore_marker(markerPath);

  EXPECT_FALSE(Poco::File(markerPath).exists());
}

TEST(PendingRestoreMarkerTest, RemoveIsANoOpWhenFileIsMissing) {
  const auto markerPath = Poco::Path(Poco::Path::current())
                              .append("tmp_pending_restore_noop.json")
                              .toString();
  if (Poco::File(markerPath).exists()) {
    Poco::File(markerPath).remove();
  }

  EXPECT_NO_THROW(pcm::backup::remove_pending_restore_marker(markerPath));
}

TEST(RestoreServiceTest, RestoresSeriesOccurrenceReminderState) {
  auto sourceDb = makeTestDatabase("tmp_restore_reminder_source");
  DuckEventSeries series;
  series.name = std::string{"Weekly Session"};
  series.start_date = 1730000000000;
  series.end_date = 1740000000000;
  series.duration = 3600;
  series.recurrence_rule = "FREQ=WEEKLY;INTERVAL=1";
  const auto seriesId = sourceDb.add_event_series(series);
  ASSERT_GT(seriesId, 0);
  ASSERT_TRUE(sourceDb.mark_series_occurrence_reminder_notified(
      seriesId, 1730000000000, 1730000000000));

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_reminder.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_reminder_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto notified =
      restoredDb.get_notified_series_occurrences_for_range(0, 1730000000001);
  EXPECT_TRUE(notified.contains({seriesId, 1730000000000}));

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_reminder_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresLinkedNoteFields) {
  auto sourceDb = makeTestDatabase("tmp_restore_note_link_source");

  DuckClient client;
  client.name = std::string{"Ivy"};
  client.last_name = std::string{"I"};
  const auto clientId = sourceDb.add_client(client);
  ASSERT_GT(clientId, 0);

  DuckEvent event;
  event.name = std::string{"Session"};
  event.start_date = 1730000000000;
  event.end_date = 1730003600000;
  const auto eventId = sourceDb.add_event(event);
  ASSERT_GT(eventId, 0);

  DuckClientNote note;
  note.client_id = clientId;
  note.body_markdown = std::string{"Linked note"};
  note.linked_event_id = eventId;
  const auto noteId = sourceDb.add_client_note(note);
  ASSERT_GT(noteId, 0);

  const auto backupPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_note_link.psybackup")
                              .toString();
  if (Poco::File(backupPath).exists()) {
    Poco::File(backupPath).remove();
  }
  ASSERT_TRUE(pcm::backup::BackupService{}.create_backup(sourceDb, backupPath).ok);

  const auto targetPath = Poco::Path(Poco::Path::current())
                              .append("tmp_restore_note_link_target")
                              .toString();
  if (Poco::File(targetPath).exists()) {
    Poco::File(targetPath).remove(true);
  }

  const auto restoreResult =
      pcm::backup::RestoreService{}.restore_backup(backupPath, targetPath);
  ASSERT_TRUE(restoreResult.ok) << restoreResult.error;

  pcm::config::Config targetConfig{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(targetPath)}};
  pcm::database::Database restoredDb{targetConfig};
  const auto notes = restoredDb.get_client_notes(clientId);
  ASSERT_EQ(notes.size(), 1);
  ASSERT_TRUE(notes.front().linked_event_id.has_value());
  EXPECT_EQ(*notes.front().linked_event_id, eventId);

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_note_link_source"))
      .remove(true);
}

TEST(RestoreServiceTest, RestoresEventChangeLogRows) {
  auto sourceDb = makeTestDatabase("tmp_restore_changelog_source");

  DuckClient client;
  client.name = std::string{"Jack"};
  client.last_name = std::string{"J"};
  const auto clientId = sourceDb.add_client(client);
  ASSERT_GT(clientId, 0);

  DuckEvent event;
  event.name = std::string{"Session"};
  event.start_date = 1730000000000;
  event.end_date = 1730003600000;
  event.event_stat_id = 1;
  event.payment_stat_id = 1;
  const auto eventId = sourceDb.add_event(event);
  ASSERT_GT(eventId, 0);
  ASSERT_GT(sourceDb.add_event_client(eventId, clientId), 0);

  DuckEvent updated;
  updated.id = eventId;
  updated.start_date = 1730000000000;
  updated.end_date = 1730003600000;
  updated.event_stat_id = 2;
  updated.payment_stat_id = 2;
  ASSERT_TRUE(sourceDb.update_event(updated));
  // Re-establish client link after update (update_event unlinks clients)
  ASSERT_GT(sourceDb.add_event_client(eventId, clientId), 0);
  ASSERT_EQ(sourceDb.get_event_change_log_for_client(clientId).size(), 2);

  auto restoredDb = backupAndRestore(sourceDb, "tmp_restore_changelog");
  ASSERT_TRUE(restoredDb.has_value());
  const auto entries = restoredDb->get_event_change_log_for_client(clientId);
  ASSERT_EQ(entries.size(), 2);

  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_changelog.psybackup"))
      .remove();
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_changelog_target"))
      .remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_changelog_source"))
      .remove(true);
}

TEST(BackupRotationServiceTest, KeepsNewestAndRemovesOlderMatchingFiles) {
  const auto dir =
      Poco::Path(Poco::Path::current()).append("tmp_rotation_dir").toString();
  if (Poco::File(dir).exists()) {
    Poco::File(dir).remove(true);
  }
  Poco::File(dir).createDirectories();

  const std::vector<std::string> autoNames = {
      "Sessio-auto-20260101-000000.psybackup",
      "Sessio-auto-20260102-000000.psybackup",
      "Sessio-auto-20260103-000000.psybackup",
      "Sessio-auto-20260104-000000.psybackup",
      "Sessio-auto-20260105-000000.psybackup",
  };
  for (const auto &name : autoNames) {
    std::ofstream(Poco::Path(dir).append(name).toString()) << "x";
  }
  // A manual backup in the same directory must never be touched by rotation.
  const auto manualName =
      Poco::Path(dir).append("Sessio-20260106-000000.psybackup").toString();
  std::ofstream(manualName) << "x";

  pcm::backup::BackupRotationService service;
  const auto result = service.prune(dir, "Sessio-auto-", 3);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.removed_count, 2);

  EXPECT_FALSE(Poco::File(Poco::Path(dir).append(autoNames[0]).toString()).exists());
  EXPECT_FALSE(Poco::File(Poco::Path(dir).append(autoNames[1]).toString()).exists());
  EXPECT_TRUE(Poco::File(Poco::Path(dir).append(autoNames[2]).toString()).exists());
  EXPECT_TRUE(Poco::File(Poco::Path(dir).append(autoNames[3]).toString()).exists());
  EXPECT_TRUE(Poco::File(Poco::Path(dir).append(autoNames[4]).toString()).exists());
  EXPECT_TRUE(Poco::File(manualName).exists());

  Poco::File(dir).remove(true);
}

TEST(BackupRotationServiceTest, NonExistentDirectoryIsNotAnError) {
  pcm::backup::BackupRotationService service;
  const auto result =
      service.prune("tmp_rotation_dir_missing", "Sessio-auto-", 3);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.removed_count, 0);
}

TEST(AutoBackupDueTest, DisabledIsNeverDue) {
  EXPECT_FALSE(pcm::backup::isAutoBackupDue(false, 0, 7, 999'999'999'999));
}

TEST(AutoBackupDueTest, NeverRunIsDueImmediatelyWhenEnabled) {
  EXPECT_TRUE(pcm::backup::isAutoBackupDue(true, 0, 7, 1'000));
}

TEST(AutoBackupDueTest, NotYetDueWithinInterval) {
  const std::int64_t lastRun = 1'700'000'000'000;
  const std::int64_t sixDaysLater = lastRun + 6LL * 24 * 60 * 60 * 1000;
  EXPECT_FALSE(pcm::backup::isAutoBackupDue(true, lastRun, 7, sixDaysLater));
}

TEST(AutoBackupDueTest, DueOnceIntervalElapsed) {
  const std::int64_t lastRun = 1'700'000'000'000;
  const std::int64_t sevenDaysLater = lastRun + 7LL * 24 * 60 * 60 * 1000;
  EXPECT_TRUE(pcm::backup::isAutoBackupDue(true, lastRun, 7, sevenDaysLater));
}

TEST(AutoBackupDueTest, EncryptedAutoBackupIsSkippedWhenKeyUnavailable) {
  BackupSettingsGuard settingsGuard;
  const auto destination = tempPath("tmp_auto_backup_key_unavailable");
  if (Poco::File(destination).exists()) {
    Poco::File(destination).remove(true);
  }
  auto database = std::make_shared<pcm::database::Database>(
      makeTestDatabase("tmp_auto_backup_key_unavailable_db"));
  InMemoryCredentialStore credentialStore;
  credentialStore.available = false;
  pcm::backup::AutoBackupScheduler scheduler(database, nullptr, &credentialStore);
  bool finished = false;
  bool ok = true;
  QObject::connect(&scheduler, &pcm::backup::AutoBackupScheduler::backupFinished,
                   [&finished, &ok](const bool result, const QString &) {
                     finished = true;
                     ok = result;
                   });

  pcm::app_settings::setAutoBackupEnabled(true);
  pcm::app_settings::setAutoBackupLastRunAtMs(0);
  pcm::app_settings::setAutoBackupDestination(QString::fromStdString(destination));
  pcm::app_settings::setBackupEncryptionEnabled(true);
  const auto workspaceUuid =
      QString::fromStdString(database->get_application_metadata().workspace_uuid);
  ASSERT_FALSE(workspaceUuid.isEmpty());
  pcm::app_settings::setBackupEncryptionKeychainEntry(
      pcm::backup::workspaceBackupKeychainEntry(workspaceUuid));
  pcm::app_settings::setBackupEncryptionRecoveryEnvelope(
      QString::fromStdString(pcm::backup::serialize_recovery_envelope(recoveryEnvelope())));
  scheduler.runAsync();

  EXPECT_FALSE(pcm::backup::automaticEncryptedBackupAllowed(
      true, pcm::backup::BackupKeySource::Unavailable));
  EXPECT_EQ(credentialStore.readCalls, 1);
  EXPECT_TRUE(finished);
  EXPECT_FALSE(ok);
  EXPECT_FALSE(Poco::File(destination).exists());
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_auto_backup_key_unavailable_db")
                 .toString())
      .remove(true);
}

TEST(AutoBackupDueTest, EncryptedAutoBackupUsesStoredEnvelopeWithoutSessionPassword) {
  auto &application = testApplication();
  BackupSettingsGuard settingsGuard;
  const auto destination = tempPath("tmp_auto_backup_key_available");
  if (Poco::File(destination).exists()) {
    Poco::File(destination).remove(true);
  }
  ASSERT_TRUE(QDir().mkpath(pcm::app_settings::attachmentsStorageRoot()));
  auto database = std::make_shared<pcm::database::Database>(
      makeTestDatabase("tmp_auto_backup_key_available_db"));
  InMemoryCredentialStore credentialStore;
  pcm::backup::AutoBackupScheduler scheduler(database, nullptr, &credentialStore);
  bool finished = false;
  bool ok = false;
  QString error;
  QObject::connect(&scheduler, &pcm::backup::AutoBackupScheduler::backupFinished,
                   [&finished, &ok, &error](const bool result, const QString &resultError) {
                     finished = true;
                     ok = result;
                     error = resultError;
                   });

  pcm::app_settings::setAutoBackupEnabled(true);
  pcm::app_settings::setAutoBackupLastRunAtMs(0);
  pcm::app_settings::setAutoBackupDestination(QString::fromStdString(destination));
  pcm::app_settings::setBackupEncryptionEnabled(true);
  const auto workspaceUuid =
      QString::fromStdString(database->get_application_metadata().workspace_uuid);
  ASSERT_FALSE(workspaceUuid.isEmpty());
  pcm::app_settings::setBackupEncryptionKeychainEntry(
      pcm::backup::workspaceBackupKeychainEntry(workspaceUuid));
  pcm::app_settings::setBackupEncryptionRecoveryEnvelope(
      QString::fromStdString(pcm::backup::serialize_recovery_envelope(recoveryEnvelope())));

  scheduler.runAsync();
  QElapsedTimer timeout;
  timeout.start();
  while (!finished && timeout.elapsed() < 10'000) {
    application.processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(5);
  }

  EXPECT_TRUE(pcm::backup::automaticEncryptedBackupAllowed(
      true, pcm::backup::BackupKeySource::Keychain));
  EXPECT_EQ(credentialStore.readCalls, 1);
  EXPECT_TRUE(finished);
  EXPECT_TRUE(ok) << error.toStdString();
  EXPECT_TRUE(error.isEmpty());
  const QDir backupDirectory(QString::fromStdString(destination));
  const auto entries = backupDirectory.entryList({"*.psybackup"}, QDir::Files);
  ASSERT_EQ(entries.size(), 1);
  EXPECT_TRUE(pcm::backup::BackupValidator{}
                  .validate(backupDirectory.filePath(entries.front()).toStdString(),
                            testRecoveryPhrase())
                  .ok);
  Poco::File(destination).remove(true);
  Poco::File(Poco::Path(Poco::Path::current())
                 .append("tmp_auto_backup_key_available_db")
                 .toString())
      .remove(true);
}

TEST(AutoBackupDueTest, RecoveryOnlyKeyNeverEnablesAutomaticBackup) {
  EXPECT_FALSE(pcm::backup::automaticEncryptedBackupAllowed(
      true, pcm::backup::BackupKeySource::RecoveryOnly));
}
