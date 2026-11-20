#include "call_event_resolver.h"

#include "database.h"
#include "recurrence_utils.h"

namespace pcm::calltranscription {

CallEventResolver::CallEventResolver(std::shared_ptr<pcm::database::Database> db,
                                     Materialise materialise)
    : mDb(std::move(db)), mMaterialise(std::move(materialise)) {}

std::optional<int64_t> CallEventResolver::resolve(const int64_t eventId) {
  if (eventId > 0) {
    return eventId;
  }
  if (eventId == 0 || !mDb || !mMaterialise) {
    return std::nullopt;
  }

  // Idempotency first: an occurrence materialised earlier stays resolvable even when the
  // series was deactivated or its rule edited since.
  if (const auto existing = pcm::recurrence::materialisedEventForVirtualId(*mDb, eventId)) {
    return existing;
  }

  auto occurrence = pcm::recurrence::virtualOccurrenceForId(*mDb, eventId);
  if (!occurrence.has_value() || !occurrence->series_id.has_value() ||
      !occurrence->start_date.has_value()) {
    return std::nullopt;
  }

  if (const auto existing =
          mDb->get_event_by_series_occurrence(*occurrence->series_id, *occurrence->start_date)) {
    return existing->id;
  }

  const auto series = mDb->get_event_series(*occurrence->series_id);
  occurrence->id = -1;
  occurrence->is_virtual_occurrence = false;
  const auto newId = mMaterialise(*occurrence);
  if (newId <= 0) {
    return std::nullopt;
  }
  if (series && series->client_id.has_value()) {
    mDb->add_event_client(newId, *series->client_id);
  }
  return newId;
}

} // namespace pcm::calltranscription
