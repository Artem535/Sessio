#include "series_schedule_committer.h"

#include "schedule/schedule.h"
#include "schedule_snapshot.h"

namespace pcm::meeting {

SeriesScheduleCommitter::Result
SeriesScheduleCommitter::commit(const pcm::database::ScheduleMutation &mutation,
                                const std::string &timezone) {
  Result result;
  std::string rejection;
  const auto builder = [&rejection](const pcm::database::ScheduleSource &source)
      -> std::optional<std::string> {
    auto snapshot = buildScheduleSnapshot(source);
    if (!snapshot.has_value()) {
      rejection = "unknown timezone or incomplete series times";
      return std::nullopt;
    }
    auto probe = *snapshot;
    probe.revision = 1; // validate the content as the server will see the first write
    probe.baseRevision = 0;
    if (const auto validation = pcm::schedule::validate(probe); !validation.valid) {
      rejection = validation.error;
      return std::nullopt;
    }
    return serializeSnapshot(*snapshot).toStdString();
  };

  const auto commit = mDb.commit_schedule_change(mutation, timezone, builder);
  if (!commit.has_value()) {
    result.error = rejection.empty() ? QStringLiteral("local_failure")
                                     : QStringLiteral("invalid_schedule");
    result.detail = QString::fromStdString(rejection);
    return result;
  }
  result.ok = true;
  result.seriesId = commit->series_id;
  result.seriesUid = QString::fromStdString(commit->series_uid);
  if (!result.seriesUid.isEmpty() && mSync) {
    mSync->notifyLocalChange(result.seriesUid);
  }
  return result;
}

bool SeriesScheduleCommitter::isPublished(const int64_t seriesId) {
  return seriesId > 0 && mDb.get_schedule_identity(seriesId).has_value();
}

} // namespace pcm::meeting
