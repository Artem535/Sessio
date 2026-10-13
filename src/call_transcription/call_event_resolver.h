#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "schema.hpp"

namespace pcm::database {
class Database;
}

namespace pcm::calltranscription {

// Turns the id of a calendar entry a call is attached to into a real event id.
// Recurring occurrences that were never edited only exist virtually (negative id);
// a transcript needs a persisted event, so such an occurrence is materialised first.
class CallEventResolver {
public:
  // Persists a virtual occurrence as a real event (production: QTimelineModel::addEvent so
  // published series get their schedule bookkeeping) and returns the new id or <= 0.
  using Materialise = std::function<int64_t(const DuckEvent &)>;

  CallEventResolver(std::shared_ptr<pcm::database::Database> db, Materialise materialise);

  // id > 0: returned as is. id < 0: decode, reuse an already materialised occurrence,
  // otherwise materialise and link the series' client. std::nullopt when the series or
  // the occurrence no longer exists or materialisation fails. Idempotent.
  std::optional<int64_t> resolve(int64_t eventId);

private:
  std::shared_ptr<pcm::database::Database> mDb;
  Materialise mMaterialise;
};

} // namespace pcm::calltranscription
