#pragma once

#include "db/accounts_repository.h"
#include "db/sqlite_connection.h"
#include "schedule/schedule.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

std::optional<std::string> normalizeSeriesUid(const std::string &uid);

struct StoredSchedule {
  std::string seriesUid;
  AccountId accountId;
  schedule::Snapshot snapshot;
  std::string contentHash;
};

enum class ScheduleWriteStatus { Applied, Conflict, NotFound, Invalid };
struct ScheduleWriteResult {
  ScheduleWriteStatus status;
  std::optional<StoredSchedule> value;
};

// Every entry point serializes connection access. put owns one transaction;
// callers must not wrap it in another transaction on this connection.
class ScheduleRepository {
public:
  explicit ScheduleRepository(SqliteConnection &conn) : conn_(conn) {}
  std::optional<StoredSchedule> get(AccountId accountId, const std::string &seriesUid);
  ScheduleWriteResult put(AccountId accountId, const std::string &seriesUid,
                          const schedule::Snapshot &snapshot);
private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
