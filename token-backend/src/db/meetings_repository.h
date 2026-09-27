#pragma once

#include "db/accounts_repository.h"
#include "db/sqlite_connection.h"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

struct Meeting {
  int64_t id;
  std::string meetingRef;
  AccountId accountId;
  std::string roomName;
  std::string scheduledStart;
  std::string scheduledEnd;
  std::string status;
};

// Thread safety: every public method below takes SqliteConnection::lock() for
// its whole body, so calls from oat++'s worker threads are serialized against
// each other and against the other repositories sharing the same connection.
class MeetingsRepository {
public:
  explicit MeetingsRepository(SqliteConnection &conn) : conn_(conn) {}

  Meeting create(AccountId accountId, const std::string &scheduledStart,
                  const std::string &scheduledEnd);
  std::optional<Meeting> findByRef(const std::string &meetingRef);
  std::optional<Meeting> findById(int64_t meetingId);
  void invalidate(int64_t meetingId);

private:
  SqliteConnection &conn_;
};

} // namespace pcm::tokenbackend
