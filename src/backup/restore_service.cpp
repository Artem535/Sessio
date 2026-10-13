#include "restore_service.h"

#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/Timestamp.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/Zip/Decompress.h>
#include <duckdb.hpp>
#include <fstream>
#include <rfl/json.hpp>

#include "backup_manifest.hpp"
#include "backup_validator.h"
#include "database.h"

namespace pcm::backup {
namespace {

struct ScratchGuard {
  std::string path;
  ~ScratchGuard() {
    try {
      Poco::File file(path);
      if (file.exists()) {
        file.remove(true);
      }
    } catch (...) {
    }
  }
};

std::string escapeSqlPath(const std::string &path) {
  std::string escaped;
  escaped.reserve(path.size());
  for (const char ch : path) {
    if (ch == '\'') {
      escaped += "''";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

bool isSafeRelativePath(const std::string &path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\' ||
      path.find(':') != std::string::npos) {
    return false;
  }

  std::string normalized = path;
  for (char &ch : normalized) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  std::size_t segmentStart = 0;
  while (segmentStart < normalized.size()) {
    const auto separator = normalized.find('/', segmentStart);
    const auto segment = normalized.substr(
        segmentStart, separator == std::string::npos
                          ? std::string::npos
                          : separator - segmentStart);
    if (segment == "..") {
      return false;
    }
    if (separator == std::string::npos) {
      break;
    }
    segmentStart = separator + 1;
  }
  return true;
}

bool extractArchive(const std::string &backupPath, const std::string &targetDir) {
  std::ifstream input(backupPath, std::ios::binary);
  if (!input) {
    return false;
  }

  Poco::Zip::Decompress decompress(input, Poco::Path(targetDir));
  decompress.decompressAllFiles();
  return true;
}

bool importDatabaseSnapshot(const std::string &snapshotDir,
                            const std::string &databaseRoot) {
  Poco::File(databaseRoot).createDirectories();
  const auto databasePath =
      Poco::Path(databaseRoot).append("database.db").toString();
  duckdb::DuckDB restoredDatabase(databasePath);
  duckdb::Connection connection(restoredDatabase);
  const auto query = "IMPORT DATABASE '" + escapeSqlPath(snapshotDir) + "';";
  auto result = connection.Query(query);
  return result && !result->HasError();
}

std::string readManifest(const std::string &extractDir,
                         BackupManifest &manifest) {
  const auto manifestPath =
      Poco::Path(extractDir).append("manifest.json").toString();
  const auto parsed = rfl::json::load<BackupManifest>(manifestPath);
  if (!parsed) {
    return "failed to parse manifest.json: " +
           std::string(parsed.error().what());
  }
  manifest = parsed.value();
  return {};
}

} // namespace

bool write_pending_restore_marker(const std::string &marker_path,
                                  const std::string &backup_path) {
  PendingRestoreMarker marker;
  marker.backup_path = backup_path;
  const auto saveResult =
      rfl::json::save(marker_path, marker, rfl::json::pretty);
  return static_cast<bool>(saveResult);
}

std::optional<PendingRestoreMarker>
read_pending_restore_marker(const std::string &marker_path) {
  if (!Poco::File(marker_path).exists()) {
    return std::nullopt;
  }
  const auto parsed = rfl::json::load<PendingRestoreMarker>(marker_path);
  if (!parsed) {
    return std::nullopt;
  }
  return parsed.value();
}

void remove_pending_restore_marker(const std::string &marker_path) {
  try {
    Poco::File file(marker_path);
    if (file.exists()) {
      file.remove();
    }
  } catch (...) {
  }
}

RestoreResult RestoreService::restore_backup(const std::string &backup_path,
                                             const std::string &database_root,
                                             const RestoreOptions &options) {
  RestoreResult result;
  try {
    std::string normalizationError;
    auto normalized = detail::normalize_backup_file(
        backup_path, options.recovery_password, &normalizationError);
    if (!normalized.has_value()) {
      result.error = "backup validation failed: " + normalizationError;
      return result;
    }

    const auto validation = BackupValidator{}.validate(normalized->zip_path());
    if (!validation.ok) {
      result.error = "backup validation failed: ";
      for (std::size_t i = 0; i < validation.errors.size(); ++i) {
        if (i != 0) {
          result.error += "; ";
        }
        result.error += validation.errors[i];
      }
      return result;
    }

    const auto uuid =
        Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    const auto extractDir =
        Poco::Path(Poco::Path::temp()).append("psyrestore-" + uuid).toString();
    ScratchGuard extractGuard{extractDir};
    Poco::File(extractDir).createDirectories();
    if (!extractArchive(normalized->zip_path(), extractDir)) {
      result.error = "failed to extract backup archive";
      return result;
    }

    BackupManifest manifest;
    if (const auto error = readManifest(extractDir, manifest); !error.empty()) {
      result.error = error;
      return result;
    }
    if (manifest.psybackup_format_version != 1 ||
        (manifest.schema_version < 1 || manifest.schema_version > 2) || manifest.backup_format_version != 1) {
      result.error = "backup format or schema version is not supported";
      return result;
    }
    for (const auto &entry : manifest.entries) {
      if (!isSafeRelativePath(entry.path)) {
        result.error = "unsafe path in backup manifest: " + entry.path;
        return result;
      }
    }

    const auto snapshotDir =
        Poco::Path(extractDir).append("database").toString();
    if (!Poco::File(Poco::Path(snapshotDir).append("schema.sql")).exists() ||
        !Poco::File(Poco::Path(snapshotDir).append("load.sql")).exists()) {
      result.error = "backup is missing the database export";
      return result;
    }

    const bool hasAttachments = manifest.kind == "database_and_attachments";
    if (hasAttachments && !options.attachments_root.has_value()) {
      result.error = "attachments destination is required for this backup";
      return result;
    }
    if (manifest.kind != "database" && !hasAttachments) {
      result.error = "unsupported backup kind: " + manifest.kind;
      return result;
    }

    const auto stagedDatabase = database_root + ".restore-partial-" + uuid;
    ScratchGuard stagedDatabaseGuard{stagedDatabase};
    if (!importDatabaseSnapshot(snapshotDir, stagedDatabase)) {
      result.error = "failed to import database snapshot";
      return result;
    }
    // Validate and migrate before replacing the workspace with a legacy snapshot.
    {
      pcm::config::Config conf;
      conf.db_conf.value_.db_pth = Poco::Path(stagedDatabase);
      pcm::database::Database migrated(conf);
      if (migrated.get_application_metadata().schema_version != 2) {
        result.error = "restored database schema is not supported";
        return result;
      }
    }

    std::string stagedAttachments;
    if (hasAttachments) {
      stagedAttachments = *options.attachments_root + ".restore-partial-" + uuid;
      ScratchGuard stagedAttachmentsGuard{stagedAttachments};
      const auto sourceAttachments =
          Poco::Path(extractDir).append("attachments").toString();
      if (!Poco::File(sourceAttachments).exists() ||
          !Poco::File(sourceAttachments).isDirectory()) {
        result.error = "backup manifest declares attachments but archive has none";
        return result;
      }
      Poco::File(sourceAttachments).copyTo(stagedAttachments);

      const auto protectiveAttachments =
          *options.attachments_root + ".pre-restore-" + uuid;
      const auto protectiveDatabase =
          database_root + ".pre-restore-" + uuid;
      bool oldDatabaseMoved = false;
      bool oldAttachmentsMoved = false;
      bool newDatabaseMoved = false;
      bool newAttachmentsMoved = false;
      try {
        Poco::File oldDatabase(database_root);
        if (oldDatabase.exists()) {
          oldDatabase.renameTo(protectiveDatabase);
          oldDatabaseMoved = true;
        }
        Poco::File oldAttachments(*options.attachments_root);
        if (oldAttachments.exists()) {
          oldAttachments.renameTo(protectiveAttachments);
          oldAttachmentsMoved = true;
        }
        Poco::File(stagedDatabase).renameTo(database_root);
        newDatabaseMoved = true;
        Poco::File(stagedAttachments).renameTo(*options.attachments_root);
        newAttachmentsMoved = true;
      } catch (...) {
        try {
          if (newAttachmentsMoved) {
            Poco::File(*options.attachments_root).remove(true);
          }
          if (newDatabaseMoved) {
            Poco::File(database_root).remove(true);
          }
          if (oldAttachmentsMoved) {
            Poco::File(protectiveAttachments).renameTo(*options.attachments_root);
          }
          if (oldDatabaseMoved) {
            Poco::File(protectiveDatabase).renameTo(database_root);
          }
        } catch (...) {
        }
        result.error = "failed to replace database and attachments";
        return result;
      }
      result.protective_attachments_path =
          oldAttachmentsMoved ? protectiveAttachments : std::string{};
      result.protective_database_path =
          oldDatabaseMoved ? protectiveDatabase : std::string{};
    } else {
      const auto protectiveDatabase =
          database_root + ".pre-restore-" + uuid;
      bool oldDatabaseMoved = false;
      bool newDatabaseMoved = false;
      try {
        Poco::File oldDatabase(database_root);
        if (oldDatabase.exists()) {
          oldDatabase.renameTo(protectiveDatabase);
          oldDatabaseMoved = true;
        }
        Poco::File(stagedDatabase).renameTo(database_root);
        newDatabaseMoved = true;
      } catch (...) {
        try {
          if (newDatabaseMoved) {
            Poco::File(database_root).remove(true);
          }
          if (oldDatabaseMoved) {
            Poco::File(protectiveDatabase).renameTo(database_root);
          }
        } catch (...) {
        }
        result.error = "failed to replace database";
        return result;
      }
      result.protective_database_path =
          oldDatabaseMoved ? protectiveDatabase : std::string{};
    }

    result.ok = true;
    return result;
  } catch (const Poco::Exception &ex) {
    result.error = "restore failed: " + ex.displayText();
    return result;
  } catch (const std::exception &ex) {
    result.error = "restore failed: " + std::string(ex.what());
    return result;
  }
}

} // namespace pcm::backup
