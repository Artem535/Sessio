#pragma once

#include <QString>

#include <optional>

// Lives with the token client (not the meeting library) because the Calls page,
// which must stay free of the database, parses it.
namespace pcm::meeting {

// Identifies one occurrence of a published series for the specialist join
// flow. The Calls tab, the event editor and the upcoming-meetings list all
// pass a single "meeting reference" string around; a series occurrence is
// carried in that same slot in this reserved form, so the existing native
// device-check / join / leave flow is reused unchanged. The occurrence is
// addressed by its ORIGINAL start (the key before any move), never by its
// displayed time.
struct SeriesJoinTarget {
  QString seriesUid;
  qint64 originalStartMs = 0;
};

// "series:<uid>@<YYYY-MM-DDTHH:MM:SSZ>"
[[nodiscard]] QString encodeSeriesJoinTarget(const QString &seriesUid, qint64 originalStartMs);
// nullopt for ordinary meeting references (single meetings, external links).
[[nodiscard]] std::optional<SeriesJoinTarget> parseSeriesJoinTarget(const QString &reference);

} // namespace pcm::meeting
