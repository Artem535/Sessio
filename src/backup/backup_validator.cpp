// backup_validator.cpp
#include "backup_validator.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/RecursiveDirectoryIterator.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/Zip/Decompress.h>
#include <algorithm>
#include <fstream>
#include <rfl/json.hpp>
#include <set>
#include <utility>

#include "backup_manifest.hpp"
#include "checksum_utils.hpp"
#include "encrypted_container.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace pcm::backup {
namespace {

void removeScratchPath(const std::string &path) {
  if (path.empty()) {
    return;
  }
  try {
    Poco::File f(path);
    if (f.exists()) {
      f.remove(true);
    }
  } catch (...) {
  }
}

struct ScratchGuard {
  std::string path;
  ~ScratchGuard() {
    try {
      Poco::File f(path);
      if (f.exists()) {
        f.remove(true);
      }
    } catch (...) {
    }
  }
};

std::string toRelative(const std::string &base, const std::string &full) {
  std::string rel = full.substr(base.size());
  if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
    rel.erase(0, 1);
  }
  std::replace(rel.begin(), rel.end(), '\\', '/');
  return rel;
}

} // namespace

namespace detail {

NormalizedBackupFile::NormalizedBackupFile(std::string zip_path,
                                           std::string scratch_path)
    : mZipPath(std::move(zip_path)), mScratchPath(std::move(scratch_path)) {}

NormalizedBackupFile::~NormalizedBackupFile() { removeScratchPath(mScratchPath); }

NormalizedBackupFile::NormalizedBackupFile(NormalizedBackupFile &&other) noexcept
    : mZipPath(std::move(other.mZipPath)),
      mScratchPath(std::move(other.mScratchPath)) {
  other.mScratchPath.clear();
}

NormalizedBackupFile &NormalizedBackupFile::operator=(
    NormalizedBackupFile &&other) noexcept {
  if (this != &other) {
    removeScratchPath(mScratchPath);
    mZipPath = std::move(other.mZipPath);
    mScratchPath = std::move(other.mScratchPath);
    other.mScratchPath.clear();
  }
  return *this;
}

const std::string &NormalizedBackupFile::zip_path() const { return mZipPath; }

std::optional<NormalizedBackupFile>
normalize_backup_file(const std::string &backup_path,
                      const std::optional<std::string> &recovery_password,
                      std::string *error) {
  const auto kind = detect_backup_container(backup_path);
  if (kind == BackupContainerKind::PlainZip) {
    return NormalizedBackupFile{backup_path};
  }
  if (kind != BackupContainerKind::Encrypted || !recovery_password.has_value()) {
    *error = kind == BackupContainerKind::Encrypted ? "cannot decrypt backup"
                                                     : "unsupported backup container";
    return std::nullopt;
  }

  const auto uuid = Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
  const auto scratch_path = Poco::Path(Poco::Path::temp())
                                .append("psybackup-decrypt-" + uuid)
                                .toString();
  try {
    Poco::File(scratch_path).createDirectories();
#ifndef _WIN32
    ::chmod(scratch_path.c_str(), S_IRWXU);
#endif
    const auto zip_path = Poco::Path(scratch_path).append("backup.zip").toString();
    const auto decryption =
        decrypt_backup_file(backup_path, zip_path, *recovery_password);
    if (!decryption.ok) {
      removeScratchPath(scratch_path);
      *error = "cannot decrypt backup";
      return std::nullopt;
    }
    return NormalizedBackupFile{zip_path, scratch_path};
  } catch (...) {
    removeScratchPath(scratch_path);
    *error = "cannot decrypt backup";
    return std::nullopt;
  }
}

} // namespace detail

ValidationResult BackupValidator::validate(
    const std::string &backup_path,
    const std::optional<std::string> &recovery_password) {
  ValidationResult result;

  try {
    Poco::File backupFile(backup_path);
    if (!backupFile.exists() || !backupFile.isFile()) {
      result.errors.push_back("backup file does not exist: " + backup_path);
      return result;
    }

    std::string normalizationError;
    auto normalized = detail::normalize_backup_file(backup_path, recovery_password,
                                                    &normalizationError);
    if (!normalized.has_value()) {
      result.errors.push_back(std::move(normalizationError));
      return result;
    }

    const auto uuid =
        Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    const auto extractDir = Poco::Path(Poco::Path::temp())
                                .append("psybackup-validate-" + uuid)
                                .toString();
    ScratchGuard guard{extractDir};

    std::ifstream zipIn(normalized->zip_path(), std::ios::binary);
    if (!zipIn) {
      result.errors.push_back("cannot open backup file: " + backup_path);
      return result;
    }

    Poco::Zip::Decompress decompress(zipIn, Poco::Path(extractDir));
    decompress.decompressAllFiles();

    const auto manifestPath =
        Poco::Path(extractDir).append("manifest.json").toString();
    if (!Poco::File(manifestPath).exists()) {
      result.errors.push_back("backup is missing manifest.json");
      return result;
    }

    const auto manifestResult = rfl::json::load<BackupManifest>(manifestPath);
    if (!manifestResult) {
      result.errors.push_back("failed to parse manifest.json: " +
                              manifestResult.error().what());
      return result;
    }
    const auto &manifest = manifestResult.value();

    if (manifest.psybackup_format_version != 1 || manifest.backup_format_version != 1 ||
        manifest.schema_version < 1 || manifest.schema_version > 2) {
      result.errors.push_back(
          "unsupported backup format or schema version");
      return result;
    }

    for (const auto &entry : manifest.entries) {
      Poco::Path entryPath(extractDir);
      entryPath.append(Poco::Path(entry.path, Poco::Path::PATH_UNIX));
      Poco::File entryFile(entryPath);
      if (!entryFile.exists() || !entryFile.isFile()) {
        result.errors.push_back("missing entry: " + entry.path);
        continue;
      }
      if (static_cast<std::int64_t>(entryFile.getSize()) != entry.size_bytes) {
        result.errors.push_back("size mismatch for entry: " + entry.path);
        continue;
      }
      const auto actualHash = sha256_file(entryPath.toString());
      if (actualHash != entry.sha256) {
        result.errors.push_back("checksum mismatch for entry: " + entry.path);
      }
    }

    std::set<std::string> knownPaths;
    for (const auto &entry : manifest.entries) {
      knownPaths.insert(entry.path);
    }

    Poco::RecursiveDirectoryIterator extractedIt(extractDir);
    const Poco::RecursiveDirectoryIterator extractedEnd;
    for (; extractedIt != extractedEnd; ++extractedIt) {
      if (!extractedIt->isFile()) {
        continue;
      }
      const auto rel = toRelative(extractDir, extractedIt->path());
      if (rel == "manifest.json") {
        continue;
      }
      if (!knownPaths.contains(rel)) {
        result.errors.push_back("unexpected file not listed in manifest: " + rel);
      }
    }

    result.ok = result.errors.empty();
    return result;
  } catch (const Poco::Exception &ex) {
    result.errors.push_back("validation failed: " + ex.displayText());
    return result;
  } catch (const std::exception &ex) {
    result.errors.push_back("validation failed: " + std::string(ex.what()));
    return result;
  }
}

} // namespace pcm::backup
