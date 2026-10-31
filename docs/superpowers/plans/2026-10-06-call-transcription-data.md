# Call Transcription Data Layer (phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store call transcripts: new `Transcript` and `TranscriptPhrase` tables, `Database` methods to create, edit, finalize and delete them, cascade deletes with events, and backup/restore coverage.

**Architecture:** Follow the existing `pcm::database::Database` conventions: `Duck*` structs in `src/database/schema.hpp`, SQL constants in `src/database/constants.hpp`, methods on `Database` using `executePrepared`. New tables come from `CREATE TABLE IF NOT EXISTS` in `kCreateTables`, which already runs on every open, so existing databases migrate on first start. `EXPORT DATABASE` / `IMPORT DATABASE` (used by backups) pick the tables up without changes; a round-trip test proves it.

**Tech Stack:** C++20, DuckDB (`duckdb.hpp`), GoogleTest, Poco (temporary directories in tests).

Spec: `docs/superpowers/specs/2026-10-06-call-transcription-design.md` (section "Data model and consent"). Phases 0-1 plan: `docs/superpowers/plans/2026-10-06-call-transcription-engine.md`.

## Global Constraints

- Issue #118, branch `feat/118-call-transcription`, worktree `.worktrees/transcription`.
- Two-space indentation, `snake_case` for database APIs and schema fields (AGENTS.md). Namespace `pcm::database`; `Duck*` structs live in the global namespace as the existing ones.
- Timestamps cross the API as milliseconds since the Unix epoch (`std::int64_t`); stored as `TIMESTAMP` through `db_utils::toDuckTimestamp(ms * 1000)` and read with `db_utils::toOptionalTimestampMs`, exactly as `add_client_note` does. `start_ms` / `end_ms` of a phrase are NOT timestamps: plain `BIGINT` milliseconds from the start of the call.
- `Transcript.status` is one of `recording`, `draft`, `reviewed`. `consent_scope` is a free string; the application will use `'live_local_v1'`.
- Database ids are `INTEGER` in the tables (`GetValue<int32_t>()` when reading ids, as the existing code); the API uses `int64_t`.
- Do NOT raise `schema_version`: the existing code keeps it at 1 and `restore_service.cpp:185` rejects any other value. Schema growth in this project is by `CREATE TABLE IF NOT EXISTS` (as `ScheduleSeries`/`ScheduleOutbox`). The spec's "raises `schema_version`" is corrected in Task 4.
- Database schema changes require a migration and a restore/round-trip test (AGENTS.md): Task 4.
- No phrase text, speaker names or client data in logs: log ids, counts and error strings only.
- Do not add a separate `TranscriptRepository` class: the spec's repository is implemented as `Database` methods, following the codebase convention (corrected in Task 4).
- Build environment: the project is already configured in `build-tr` with the prebuilt vcpkg tree (never run vcpkg install or reconfigure from scratch; the container cannot build, so run commands with `distrobox-host-exec`). Reconfigure only to pick up CMakeLists changes: `distrobox-host-exec cmake -S . -B build-tr` (cache is kept). Link with `--parallel 3`. The ninja process is named `ninja-build`; never use `pkill -f`.
- Tests run on the host: `distrobox-host-exec ctest --test-dir build-tr -R <regex> --output-on-failure`. `SingleInstanceGuard` failures at `-j4` are a known unrelated socket race.
- Before each commit `git diff --check`. Do not commit build directories or the `third_party/qlementine` submodule marker. Commit message ends with the Co-Authored-By line from the session reminder.

## File Structure

```
src/database/schema.hpp                 + DuckTranscript, DuckTranscriptPhrase
src/database/constants.hpp              + tables in kCreateTables, + transcript queries
src/database/database.h / .cpp          + transcript methods, cascade in remove_event and delete_event_series_overrides_from
test/database_transcript_tests.cpp      new; all transcript data tests (own executable)
test/CMakeLists.txt                     + Sessio_database_transcript_tests
test/backup_tests.cpp                   + restore round-trip test with transcripts
docs/superpowers/specs/...-design.md    corrections (schema_version, repository)
```

## Public API (defined across Tasks 1-2; later tasks and phase 3 rely on these exact signatures)

```cpp
// database.h, class Database
int64_t add_transcript(int64_t event_id, const std::string &consent_scope,
                       const std::optional<std::string> &model_id = std::nullopt,
                       std::optional<int64_t> consent_given_at_ms = std::nullopt);
std::unique_ptr<DuckTranscript> get_transcript(int64_t id);
std::vector<DuckTranscript> get_transcripts_for_event(int64_t event_id);
bool set_transcript_status(int64_t id, const std::string &status);
bool revoke_transcript_consent(int64_t id, std::optional<int64_t> at_ms = std::nullopt);
int64_t finalize_interrupted_transcripts();
bool delete_transcript(int64_t id);
bool delete_all_transcripts();

int64_t add_transcript_phrase(const DuckTranscriptPhrase &phrase);
std::vector<DuckTranscriptPhrase> get_transcript_phrases(int64_t transcript_id);
bool update_transcript_phrase_text(int64_t phrase_id, const std::string &text);
bool delete_transcript_phrase(int64_t phrase_id);
```

---

### Task 1: Schema, structs, and transcript lifecycle methods

**Files:**
- Modify: `src/database/constants.hpp` (end of `kCreateTables`, new query constants), `src/database/schema.hpp`, `src/database/database.h`, `src/database/database.cpp`, `test/CMakeLists.txt`
- Create: `test/database_transcript_tests.cpp`

**Interfaces:**
- Produces: tables `Transcript`, `TranscriptPhrase`; `struct DuckTranscript { int64_t id; int64_t event_id; std::string status; std::string consent_scope; int64_t consent_given_at; std::optional<int64_t> consent_revoked_at; std::optional<std::string> model_id; int64_t created_at; int64_t updated_at; }` (ms) with a `DuckTranscript(const duckdb::DataChunk&, duckdb::idx_t)` constructor reading columns `id, event_id, status, consent_scope, consent_given_at, consent_revoked_at, model_id, created_at, updated_at`; the `Database` methods `add_transcript`, `get_transcript`, `get_transcripts_for_event`, `set_transcript_status`, `revoke_transcript_consent`, `finalize_interrupted_transcripts`, `delete_transcript` (phrase deletion is added in Task 2; in this task it only deletes the transcript row), `delete_all_transcripts` as in the Public API above.
- Rules: `add_transcript` returns 0 for `event_id <= 0` or when the INSERT fails (e.g. unknown event: the FK rejects it); a new transcript has status `recording`, `created_at == updated_at == now`, `consent_given_at` is the argument or now. `set_transcript_status` returns false for a status outside the three values or an unknown id (check the row exists first: an `UPDATE` of zero rows is not an error). `revoke_transcript_consent` sets `consent_revoked_at` (argument or now) and bumps `updated_at`; false for an unknown id. `finalize_interrupted_transcripts` sets status `draft` and bumps `updated_at` for every row in status `recording` and returns how many it changed (used at application start after a crash).

- [ ] **Step 1: Write the failing tests**

`test/database_transcript_tests.cpp`:

```cpp
#include <Poco/File.h>
#include <Poco/Path.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "config.h"
#include "database.h"

namespace {

// One temporary database directory per test, removed before and after.
class TranscriptDbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = Poco::Path(Poco::Path::current())
               .append(std::string("tmp_transcript_") + info->name())
               .toString();
    removeDir();
    pcm::config::Config conf{
        .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
    db_ = std::make_unique<pcm::database::Database>(conf);
  }

  void TearDown() override {
    db_.reset();
    removeDir();
  }

  void removeDir() {
    Poco::File dir(dir_);
    if (dir.exists()) dir.remove(true);
  }

  int64_t makeEvent(int64_t start_ms = 1730000000000) {
    DuckEvent event;
    event.name = std::string{"Session"};
    event.start_date = start_ms;
    event.end_date = start_ms + 3600000;
    event.duration = 3600;
    event.event_stat_id = 1;
    event.payment_stat_id = 1;
    return db_->add_event(event);
  }

  std::string dir_;
  std::unique_ptr<pcm::database::Database> db_;
};

}  // namespace

TEST_F(TranscriptDbTest, AddTranscriptStartsRecordingWithConsentTimes) {
  const auto event_id = makeEvent();
  ASSERT_GT(event_id, 0);
  const auto id = db_->add_transcript(event_id, "live_local_v1", std::string{"gigaam-v3-rnnt"},
                                      1730000100000);
  ASSERT_GT(id, 0);

  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->id, id);
  EXPECT_EQ(t->event_id, event_id);
  EXPECT_EQ(t->status, "recording");
  EXPECT_EQ(t->consent_scope, "live_local_v1");
  EXPECT_EQ(t->consent_given_at, 1730000100000);
  EXPECT_FALSE(t->consent_revoked_at.has_value());
  EXPECT_EQ(t->model_id.value_or(""), "gigaam-v3-rnnt");
  EXPECT_GT(t->created_at, 0);
  EXPECT_EQ(t->updated_at, t->created_at);
}

TEST_F(TranscriptDbTest, AddTranscriptRejectsInvalidAndUnknownEvent) {
  EXPECT_EQ(db_->add_transcript(0, "live_local_v1"), 0);
  EXPECT_EQ(db_->add_transcript(-5, "live_local_v1"), 0);
  EXPECT_EQ(db_->add_transcript(987654, "live_local_v1"), 0);  // no such event
}

TEST_F(TranscriptDbTest, ConsentGivenAtDefaultsToNow) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(id, 0);
  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  EXPECT_GT(t->consent_given_at, 1700000000000);  // some time after 2023
  EXPECT_FALSE(t->model_id.has_value());
}

TEST_F(TranscriptDbTest, GetTranscriptsForEventReturnsOnlyThatEventInCreationOrder) {
  const auto a = makeEvent(1730000000000);
  const auto b = makeEvent(1740000000000);
  const auto a1 = db_->add_transcript(a, "live_local_v1");
  const auto b1 = db_->add_transcript(b, "live_local_v1");
  const auto a2 = db_->add_transcript(a, "live_local_v1");

  const auto for_a = db_->get_transcripts_for_event(a);
  ASSERT_EQ(for_a.size(), 2u);
  EXPECT_EQ(for_a[0].id, a1);
  EXPECT_EQ(for_a[1].id, a2);
  const auto for_b = db_->get_transcripts_for_event(b);
  ASSERT_EQ(for_b.size(), 1u);
  EXPECT_EQ(for_b[0].id, b1);
  EXPECT_TRUE(db_->get_transcripts_for_event(0).empty());
}

TEST_F(TranscriptDbTest, GetTranscriptUnknownIdIsNull) {
  EXPECT_EQ(db_->get_transcript(12345), nullptr);
  EXPECT_EQ(db_->get_transcript(0), nullptr);
}

TEST_F(TranscriptDbTest, SetStatusValidatesAndUpdates) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(id, "draft"));
  EXPECT_EQ(db_->get_transcript(id)->status, "draft");
  ASSERT_TRUE(db_->set_transcript_status(id, "reviewed"));
  EXPECT_EQ(db_->get_transcript(id)->status, "reviewed");

  EXPECT_FALSE(db_->set_transcript_status(id, "published"));
  EXPECT_EQ(db_->get_transcript(id)->status, "reviewed");
  EXPECT_FALSE(db_->set_transcript_status(777, "draft"));
}

TEST_F(TranscriptDbTest, RevokeConsentRecordsTimeAndKeepsRow) {
  const auto id = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_TRUE(db_->revoke_transcript_consent(id, 1730000200000));
  const auto t = db_->get_transcript(id);
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->consent_revoked_at.has_value());
  EXPECT_EQ(*t->consent_revoked_at, 1730000200000);
  EXPECT_FALSE(db_->revoke_transcript_consent(4242));
}

TEST_F(TranscriptDbTest, FinalizeInterruptedTurnsRecordingIntoDraftOnly) {
  const auto event_id = makeEvent();
  const auto recording = db_->add_transcript(event_id, "live_local_v1");
  const auto reviewed = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(reviewed, "reviewed"));
  const auto draft = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->set_transcript_status(draft, "draft"));

  EXPECT_EQ(db_->finalize_interrupted_transcripts(), 1);
  EXPECT_EQ(db_->get_transcript(recording)->status, "draft");
  EXPECT_EQ(db_->get_transcript(reviewed)->status, "reviewed");
  EXPECT_EQ(db_->get_transcript(draft)->status, "draft");
  EXPECT_EQ(db_->finalize_interrupted_transcripts(), 0);
}

TEST_F(TranscriptDbTest, DeleteTranscriptAndDeleteAll) {
  const auto event_id = makeEvent();
  const auto one = db_->add_transcript(event_id, "live_local_v1");
  const auto two = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_TRUE(db_->delete_transcript(one));
  EXPECT_EQ(db_->get_transcript(one), nullptr);
  EXPECT_NE(db_->get_transcript(two), nullptr);
  EXPECT_FALSE(db_->delete_transcript(one));  // already gone

  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_TRUE(db_->get_transcripts_for_event(event_id).empty());
  EXPECT_TRUE(db_->delete_all_transcripts());  // nothing to delete is not an error
}
```

Register the target in `test/CMakeLists.txt` next to `Sessio_database_tests`:

```cmake
# Transcript data layer tests
add_executable(Sessio_database_transcript_tests database_transcript_tests.cpp)
target_link_libraries(Sessio_database_transcript_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    Sessio_config
    Sessio_database
    Poco::Foundation
)
gtest_discover_tests(Sessio_database_transcript_tests)
```

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake -S . -B build-tr
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests --parallel 3
```

Expected: FAIL to compile — `DuckTranscript` / `add_transcript` not declared.

- [ ] **Step 3: Add the tables and queries**

At the end of `kCreateTables` in `src/database/constants.hpp`, before the closing `)duckdb";`, after the `ScheduleOutbox` table, append (Event is created earlier in the same string):

```sql
-- Live call transcripts. status: recording | draft | reviewed. Phrase times are
-- milliseconds from the start of the call (not timestamps).
CREATE TABLE IF NOT EXISTS Transcript (
    id INTEGER PRIMARY KEY,
    event_id INTEGER NOT NULL REFERENCES Event(id),
    status TEXT NOT NULL,
    consent_scope TEXT NOT NULL,
    consent_given_at TIMESTAMP NOT NULL,
    consent_revoked_at TIMESTAMP,
    model_id TEXT,
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL
);

CREATE TABLE IF NOT EXISTS TranscriptPhrase (
    id INTEGER PRIMARY KEY,
    transcript_id INTEGER NOT NULL REFERENCES Transcript(id),
    track_role TEXT NOT NULL,
    speaker_name TEXT,
    start_ms BIGINT NOT NULL,
    end_ms BIGINT NOT NULL,
    text TEXT NOT NULL,
    edited BOOLEAN DEFAULT FALSE
);
```

After the other constants (for example after `kInsertClientNoteQuery`'s neighbours, keep the file's grouping), add:

```cpp
constexpr auto kInsertTranscriptQuery = R"duckdb(
INSERT INTO Transcript (
    id, event_id, status, consent_scope, consent_given_at, model_id,
    created_at, updated_at
)
SELECT COALESCE(MAX(id), 0) + 1, $1, 'recording', $2, $3, $4, $5, $6
FROM Transcript
RETURNING id
)duckdb";

constexpr auto kTranscriptColumns =
    "id, event_id, status, consent_scope, consent_given_at, "
    "consent_revoked_at, model_id, created_at, updated_at";

constexpr auto kSelectTranscriptByIdQuery = R"duckdb(
SELECT id, event_id, status, consent_scope, consent_given_at,
       consent_revoked_at, model_id, created_at, updated_at
FROM Transcript WHERE id = $1
)duckdb";

constexpr auto kSelectTranscriptsByEventQuery = R"duckdb(
SELECT id, event_id, status, consent_scope, consent_given_at,
       consent_revoked_at, model_id, created_at, updated_at
FROM Transcript WHERE event_id = $1 ORDER BY id
)duckdb";

constexpr auto kTranscriptExistsQuery = "SELECT 1 FROM Transcript WHERE id = $1";

constexpr auto kUpdateTranscriptStatusQuery =
    "UPDATE Transcript SET status = $1, updated_at = $2 WHERE id = $3";

constexpr auto kRevokeTranscriptConsentQuery =
    "UPDATE Transcript SET consent_revoked_at = $1, updated_at = $2 WHERE id = $3";

constexpr auto kFinalizeInterruptedTranscriptsQuery = R"duckdb(
UPDATE Transcript SET status = 'draft', updated_at = $1
WHERE status = 'recording'
RETURNING id
)duckdb";

constexpr auto kDeleteTranscriptByIdQuery = "DELETE FROM Transcript WHERE id = $1";
constexpr auto kDeleteAllTranscriptsQuery = "DELETE FROM Transcript";
```

Remove `kTranscriptColumns` if the compiler warns it is unused (the two SELECTs spell the columns out; the constant is not needed).

- [ ] **Step 4: Add the struct**

Append to `src/database/schema.hpp` (after `DuckClientNote`'s `operator<<`; keep the file's style; `print_optional` and `db_utils` are already available there):

```cpp
// --- DuckTranscript ---
// status: "recording" | "draft" | "reviewed". Times are epoch milliseconds.
struct DuckTranscript {
  std::int64_t id = -1;
  std::int64_t event_id = -1;
  std::string status;
  std::string consent_scope;
  std::int64_t consent_given_at = 0;
  std::optional<std::int64_t> consent_revoked_at = std::nullopt;
  std::optional<std::string> model_id = std::nullopt;
  std::int64_t created_at = 0;
  std::int64_t updated_at = 0;

  DuckTranscript() = default;
  DuckTranscript(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    event_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    status = chunk.GetValue(2, index).ToString();
    consent_scope = chunk.GetValue(3, index).ToString();
    consent_given_at = db_utils::toOptionalTimestampMs(chunk.GetValue(4, index)).value_or(0);
    consent_revoked_at = db_utils::toOptionalTimestampMs(chunk.GetValue(5, index));
    model_id = db_utils::toOptionalString(chunk.GetValue(6, index));
    created_at = db_utils::toOptionalTimestampMs(chunk.GetValue(7, index)).value_or(0);
    updated_at = db_utils::toOptionalTimestampMs(chunk.GetValue(8, index)).value_or(0);
  }
};
```

Do not add an `operator<<` that prints `model_id` or anything else sensitive; there is nothing sensitive in this struct, but none is needed.

- [ ] **Step 5: Add the methods**

In `src/database/database.h`, in the public section after `get_note_attachments`:

```cpp
  // --- Live call transcripts (phase 2 of #118) ---
  int64_t add_transcript(int64_t event_id, const std::string &consent_scope,
                         const std::optional<std::string> &model_id = std::nullopt,
                         std::optional<int64_t> consent_given_at_ms = std::nullopt);
  std::unique_ptr<DuckTranscript> get_transcript(int64_t id);
  std::vector<DuckTranscript> get_transcripts_for_event(int64_t event_id);
  bool set_transcript_status(int64_t id, const std::string &status);
  bool revoke_transcript_consent(int64_t id, std::optional<int64_t> at_ms = std::nullopt);
  // Marks transcripts left in "recording" (crash, power loss) as drafts and
  // returns how many were changed. Call once at application start.
  int64_t finalize_interrupted_transcripts();
  bool delete_transcript(int64_t id);
  bool delete_all_transcripts();
```

In `src/database/database.cpp`, before the `// --- Init ---` section (use `nowMs()` from the anonymous namespace at the top of the file, and `write_connection` as the other writers do):

```cpp
// --- Live call transcripts ---

int64_t Database::add_transcript(const int64_t event_id, const std::string &consent_scope,
                                 const std::optional<std::string> &model_id,
                                 const std::optional<int64_t> consent_given_at_ms) {
  if (event_id <= 0) {
    PLOG_WARNING << "Invalid event_id for Transcript: " << event_id;
    return 0;
  }
  const auto now = nowMs();
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto result = executePrepared(
      conn, constance::kInsertTranscriptQuery,
      {duckdb::Value::BIGINT(event_id), duckdb::Value(consent_scope),
       db_utils::toDuckTimestamp(consent_given_at_ms.value_or(now) * 1000),
       db_utils::toDuckValue(model_id), db_utils::toDuckTimestamp(now * 1000),
       db_utils::toDuckTimestamp(now * 1000)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert transcript (event_id=" << event_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_transcript";
    return 0;
  }
  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

std::unique_ptr<DuckTranscript> Database::get_transcript(const int64_t id) {
  if (id <= 0) return nullptr;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptByIdQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to get transcript (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return nullptr;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) return nullptr;
  return std::make_unique<DuckTranscript>(*chunk, 0);
}

std::vector<DuckTranscript> Database::get_transcripts_for_event(const int64_t event_id) {
  std::vector<DuckTranscript> out;
  if (event_id <= 0) return out;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptsByEventQuery,
                                {duckdb::Value::BIGINT(event_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list transcripts (event_id=" << event_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return out;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) out.emplace_back(*chunk, i);
  }
  return out;
}

namespace {

bool transcriptExists(duckdb::Connection &conn, const int64_t id) {
  auto result = executePrepared(conn, constance::kTranscriptExistsQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) return false;
  auto chunk = result->Fetch();
  return chunk && chunk->size() > 0;
}

}  // namespace

bool Database::set_transcript_status(const int64_t id, const std::string &status) {
  if (id <= 0 || (status != "recording" && status != "draft" && status != "reviewed")) {
    PLOG_WARNING << "Rejected transcript status change (id=" << id << ")";
    return false;
  }
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  if (!transcriptExists(conn, id)) return false;
  auto result = executePrepared(
      conn, constance::kUpdateTranscriptStatusQuery,
      {duckdb::Value(status), db_utils::toDuckTimestamp(nowMs() * 1000),
       duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to update transcript status (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

bool Database::revoke_transcript_consent(const int64_t id,
                                         const std::optional<int64_t> at_ms) {
  if (id <= 0) return false;
  const auto now = nowMs();
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  if (!transcriptExists(conn, id)) return false;
  auto result = executePrepared(
      conn, constance::kRevokeTranscriptConsentQuery,
      {db_utils::toDuckTimestamp(at_ms.value_or(now) * 1000),
       db_utils::toDuckTimestamp(now * 1000), duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to revoke transcript consent (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

int64_t Database::finalize_interrupted_transcripts() {
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto result = executePrepared(conn, constance::kFinalizeInterruptedTranscriptsQuery,
                                {db_utils::toDuckTimestamp(nowMs() * 1000)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to finalize interrupted transcripts: "
               << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  int64_t changed = 0;
  while (auto chunk = result->Fetch()) changed += static_cast<int64_t>(chunk->size());
  return changed;
}

bool Database::delete_transcript(const int64_t id) {
  if (id <= 0) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  if (!transcriptExists(conn, id)) return false;
  auto result = executePrepared(conn, constance::kDeleteTranscriptByIdQuery,
                                {duckdb::Value::BIGINT(id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete transcript (id=" << id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}

bool Database::delete_all_transcripts() {
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto result = executePrepared(conn, constance::kDeleteAllTranscriptsQuery, {});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete all transcripts: "
               << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}
```

Possible DuckDB behaviours to check while running the tests, not to guess at: (1) the unnamed namespace placed mid-file is fine, but if `executePrepared`/`nowMs` are in an anonymous namespace at the top of `database.cpp`, call them unqualified as above; (2) if `UPDATE Transcript` is rejected with a foreign-key error once phrases exist (Task 2 tests will show it), follow the contingency in Task 2 Step 5.

- [ ] **Step 6: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R TranscriptDbTest --output-on-failure
```

Expected: 8 tests PASS. Also run the existing database tests to prove nothing else changed:

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R "DatabaseTest" --output-on-failure
```

Expected: PASS (the new tables are created by `CREATE TABLE IF NOT EXISTS` on every open).

- [ ] **Step 7: Commit**

```bash
git add src/database test/CMakeLists.txt test/database_transcript_tests.cpp
git commit -m "Add Transcript table and lifecycle methods (#118)"
```

---

### Task 2: Phrases

**Files:**
- Modify: `src/database/constants.hpp`, `src/database/schema.hpp`, `src/database/database.h`, `src/database/database.cpp`, `test/database_transcript_tests.cpp`

**Interfaces:**
- Consumes: Task 1 tables and methods; the `TranscriptDbTest` fixture.
- Produces: `struct DuckTranscriptPhrase { int64_t id; int64_t transcript_id; std::string track_role; std::optional<std::string> speaker_name; int64_t start_ms; int64_t end_ms; std::string text; bool edited; }` with a `DuckTranscriptPhrase(const duckdb::DataChunk&, duckdb::idx_t)` constructor reading `id, transcript_id, track_role, speaker_name, start_ms, end_ms, text, edited`; `add_transcript_phrase`, `get_transcript_phrases`, `update_transcript_phrase_text`, `delete_transcript_phrase` as in the Public API; and `delete_transcript` / `delete_all_transcripts` now delete the phrases first.
- Rules: `add_transcript_phrase` returns 0 for `transcript_id <= 0`, empty `text`, or `end_ms < start_ms`, or an unknown transcript; `edited` is always stored as false on insert. `get_transcript_phrases` returns phrases ordered by `start_ms`, then `id`. `update_transcript_phrase_text` rejects empty text and unknown id (false), sets `text` and `edited = TRUE`, and bumps the parent transcript's `updated_at`. `delete_transcript_phrase` returns false for unknown id.

- [ ] **Step 1: Write the failing tests**

Append to `test/database_transcript_tests.cpp`:

```cpp
namespace {

DuckTranscriptPhrase phrase(int64_t transcript_id, const char *role, const char *speaker,
                            int64_t start_ms, int64_t end_ms, const char *text) {
  DuckTranscriptPhrase p;
  p.transcript_id = transcript_id;
  p.track_role = role;
  p.speaker_name = std::string{speaker};
  p.start_ms = start_ms;
  p.end_ms = end_ms;
  p.text = text;
  return p;
}

}  // namespace

TEST_F(TranscriptDbTest, PhrasesRoundTripAndAreOrderedByStartTime) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto late = db_->add_transcript_phrase(phrase(t, "participant", "Client", 5000, 7000, "second"));
  const auto early = db_->add_transcript_phrase(phrase(t, "practitioner", "Specialist", 1000, 3000, "first"));
  ASSERT_GT(late, 0);
  ASSERT_GT(early, 0);

  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 2u);
  EXPECT_EQ(phrases[0].id, early);
  EXPECT_EQ(phrases[0].transcript_id, t);
  EXPECT_EQ(phrases[0].track_role, "practitioner");
  EXPECT_EQ(phrases[0].speaker_name.value_or(""), "Specialist");
  EXPECT_EQ(phrases[0].start_ms, 1000);
  EXPECT_EQ(phrases[0].end_ms, 3000);
  EXPECT_EQ(phrases[0].text, "first");
  EXPECT_FALSE(phrases[0].edited);
  EXPECT_EQ(phrases[1].id, late);
}

TEST_F(TranscriptDbTest, PhraseWithoutSpeakerNameAndCyrillicTextRoundTrips) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  DuckTranscriptPhrase p = phrase(t, "participant", "x", 0, 900, "Привет, как дела?");
  p.speaker_name = std::nullopt;
  ASSERT_GT(db_->add_transcript_phrase(p), 0);
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_FALSE(phrases[0].speaker_name.has_value());
  EXPECT_EQ(phrases[0].text, "Привет, как дела?");
}

TEST_F(TranscriptDbTest, AddPhraseRejectsInvalidInput) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  EXPECT_EQ(db_->add_transcript_phrase(phrase(0, "participant", "C", 0, 1, "x")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1, "")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(t, "participant", "C", 500, 100, "x")), 0);
  EXPECT_EQ(db_->add_transcript_phrase(phrase(424242, "participant", "C", 0, 1, "x")), 0);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
}

TEST_F(TranscriptDbTest, UpdatePhraseTextMarksEditedAndBumpsTranscript) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto id = db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "mispelled"));
  const auto before = db_->get_transcript(t)->updated_at;

  ASSERT_TRUE(db_->update_transcript_phrase_text(id, "misspelled"));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].text, "misspelled");
  EXPECT_TRUE(phrases[0].edited);
  EXPECT_GE(db_->get_transcript(t)->updated_at, before);

  EXPECT_FALSE(db_->update_transcript_phrase_text(id, ""));
  EXPECT_FALSE(db_->update_transcript_phrase_text(31337, "x"));
  EXPECT_EQ(db_->get_transcript_phrases(t)[0].text, "misspelled");
}

TEST_F(TranscriptDbTest, DeletePhraseRemovesOnlyThatPhrase) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  const auto a = db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a"));
  const auto b = db_->add_transcript_phrase(phrase(t, "participant", "C", 2000, 3000, "b"));
  ASSERT_TRUE(db_->delete_transcript_phrase(a));
  const auto phrases = db_->get_transcript_phrases(t);
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].id, b);
  EXPECT_FALSE(db_->delete_transcript_phrase(a));
}

TEST_F(TranscriptDbTest, StatusUpdateWorksOnTranscriptThatHasPhrases) {
  const auto t = db_->add_transcript(makeEvent(), "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a")), 0);
  ASSERT_TRUE(db_->set_transcript_status(t, "draft"));
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->get_transcript(t)->status, "draft");
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, EditingAnEventKeepsItsTranscript) {
  const auto event_id = makeEvent();
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "a")), 0);

  auto event = db_->get_event(event_id);
  ASSERT_NE(event, nullptr);
  event->name = std::string{"Renamed"};
  event->event_stat_id = 2;
  ASSERT_TRUE(db_->update_event(*event));
  EXPECT_NE(db_->get_transcript(t), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, DeleteTranscriptRemovesItsPhrases) {
  const auto event_id = makeEvent();
  const auto keep = db_->add_transcript(event_id, "live_local_v1");
  const auto drop = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(keep, "participant", "C", 0, 1000, "keep")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(drop, "participant", "C", 0, 1000, "drop")), 0);

  ASSERT_TRUE(db_->delete_transcript(drop));
  EXPECT_TRUE(db_->get_transcript_phrases(drop).empty());
  EXPECT_EQ(db_->get_transcript_phrases(keep).size(), 1u);

  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_TRUE(db_->get_transcript_phrases(keep).empty());
  EXPECT_EQ(db_->get_transcript(keep), nullptr);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests --parallel 3
```

Expected: FAIL to compile — `DuckTranscriptPhrase` not declared.

- [ ] **Step 3: Queries**

Add to `src/database/constants.hpp` next to the transcript queries:

```cpp
constexpr auto kInsertTranscriptPhraseQuery = R"duckdb(
INSERT INTO TranscriptPhrase (
    id, transcript_id, track_role, speaker_name, start_ms, end_ms, text, edited
)
SELECT COALESCE(MAX(id), 0) + 1, $1, $2, $3, $4, $5, $6, FALSE
FROM TranscriptPhrase
RETURNING id
)duckdb";

constexpr auto kSelectTranscriptPhrasesQuery = R"duckdb(
SELECT id, transcript_id, track_role, speaker_name, start_ms, end_ms, text, edited
FROM TranscriptPhrase WHERE transcript_id = $1 ORDER BY start_ms, id
)duckdb";

constexpr auto kSelectTranscriptIdOfPhraseQuery =
    "SELECT transcript_id FROM TranscriptPhrase WHERE id = $1";

constexpr auto kUpdateTranscriptPhraseTextQuery =
    "UPDATE TranscriptPhrase SET text = $1, edited = TRUE WHERE id = $2";

constexpr auto kTouchTranscriptQuery =
    "UPDATE Transcript SET updated_at = $1 WHERE id = $2";

constexpr auto kDeleteTranscriptPhraseByIdQuery =
    "DELETE FROM TranscriptPhrase WHERE id = $1";
constexpr auto kDeletePhrasesByTranscriptIdQuery =
    "DELETE FROM TranscriptPhrase WHERE transcript_id = $1";
constexpr auto kDeleteAllTranscriptPhrasesQuery = "DELETE FROM TranscriptPhrase";
```

- [ ] **Step 4: Struct and methods**

`schema.hpp`, after `DuckTranscript`:

```cpp
// --- DuckTranscriptPhrase ---
// start_ms / end_ms are milliseconds from the start of the call.
struct DuckTranscriptPhrase {
  std::int64_t id = -1;
  std::int64_t transcript_id = -1;
  std::string track_role;  // "practitioner" | "participant"
  std::optional<std::string> speaker_name = std::nullopt;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;
  std::string text;
  bool edited = false;

  DuckTranscriptPhrase() = default;
  DuckTranscriptPhrase(const duckdb::DataChunk &chunk, duckdb::idx_t index) {
    id = db_utils::toInt32AsInt64(chunk.GetValue(0, index));
    transcript_id = db_utils::toInt32AsInt64(chunk.GetValue(1, index));
    track_role = chunk.GetValue(2, index).ToString();
    speaker_name = db_utils::toOptionalString(chunk.GetValue(3, index));
    start_ms = chunk.GetValue(4, index).GetValue<int64_t>();
    end_ms = chunk.GetValue(5, index).GetValue<int64_t>();
    text = chunk.GetValue(6, index).ToString();
    const auto edited_value = chunk.GetValue(7, index);
    edited = !edited_value.IsNull() && db_utils::toBool(edited_value);
  }
};
```

`database.h`:

```cpp
  int64_t add_transcript_phrase(const DuckTranscriptPhrase &phrase);
  std::vector<DuckTranscriptPhrase> get_transcript_phrases(int64_t transcript_id);
  bool update_transcript_phrase_text(int64_t phrase_id, const std::string &text);
  bool delete_transcript_phrase(int64_t phrase_id);
```

`database.cpp`, after the Task 1 transcript methods:

```cpp
int64_t Database::add_transcript_phrase(const DuckTranscriptPhrase &phrase) {
  if (phrase.transcript_id <= 0 || phrase.text.empty() || phrase.end_ms < phrase.start_ms) {
    PLOG_WARNING << "Rejected invalid transcript phrase (transcript_id="
                 << phrase.transcript_id << ")";
    return 0;
  }
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto result = executePrepared(
      conn, constance::kInsertTranscriptPhraseQuery,
      {duckdb::Value::BIGINT(phrase.transcript_id), duckdb::Value(phrase.track_role),
       db_utils::toDuckValue(phrase.speaker_name), duckdb::Value::BIGINT(phrase.start_ms),
       duckdb::Value::BIGINT(phrase.end_ms), duckdb::Value(phrase.text)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to insert transcript phrase (transcript_id=" << phrase.transcript_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return 0;
  }
  auto chunk = result->Fetch();
  if (!chunk || chunk->size() == 0) {
    PLOG_ERROR << "Empty result from RETURNING id in add_transcript_phrase";
    return 0;
  }
  return static_cast<int64_t>(chunk->GetValue(0, 0).GetValue<int32_t>());
}

std::vector<DuckTranscriptPhrase> Database::get_transcript_phrases(const int64_t transcript_id) {
  std::vector<DuckTranscriptPhrase> out;
  if (transcript_id <= 0) return out;
  duckdb::Connection conn(*mDb);
  auto result = executePrepared(conn, constance::kSelectTranscriptPhrasesQuery,
                                {duckdb::Value::BIGINT(transcript_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to list transcript phrases (transcript_id=" << transcript_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return out;
  }
  while (auto chunk = result->Fetch()) {
    for (duckdb::idx_t i = 0; i < chunk->size(); ++i) out.emplace_back(*chunk, i);
  }
  return out;
}

bool Database::update_transcript_phrase_text(const int64_t phrase_id, const std::string &text) {
  if (phrase_id <= 0 || text.empty()) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto owner = executePrepared(conn, constance::kSelectTranscriptIdOfPhraseQuery,
                               {duckdb::Value::BIGINT(phrase_id)});
  if (!owner || owner->HasError()) return false;
  auto owner_chunk = owner->Fetch();
  if (!owner_chunk || owner_chunk->size() == 0) return false;
  const auto transcript_id =
      static_cast<int64_t>(owner_chunk->GetValue(0, 0).GetValue<int32_t>());

  auto result = executePrepared(conn, constance::kUpdateTranscriptPhraseTextQuery,
                                {duckdb::Value(text), duckdb::Value::BIGINT(phrase_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to update transcript phrase (id=" << phrase_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  auto touch = executePrepared(conn, constance::kTouchTranscriptQuery,
                               {db_utils::toDuckTimestamp(nowMs() * 1000),
                                duckdb::Value::BIGINT(transcript_id)});
  if (!touch || touch->HasError()) {
    PLOG_ERROR << "Failed to bump transcript updated_at (id=" << transcript_id << ")";
    return false;
  }
  return true;
}

bool Database::delete_transcript_phrase(const int64_t phrase_id) {
  if (phrase_id <= 0) return false;
  std::optional<duckdb::Connection> ownedConn;
  auto &conn = write_connection(ownedConn);
  auto owner = executePrepared(conn, constance::kSelectTranscriptIdOfPhraseQuery,
                               {duckdb::Value::BIGINT(phrase_id)});
  if (!owner || owner->HasError()) return false;
  auto owner_chunk = owner->Fetch();
  if (!owner_chunk || owner_chunk->size() == 0) return false;
  auto result = executePrepared(conn, constance::kDeleteTranscriptPhraseByIdQuery,
                                {duckdb::Value::BIGINT(phrase_id)});
  if (!result || result->HasError()) {
    PLOG_ERROR << "Failed to delete transcript phrase (id=" << phrase_id
               << "): " << (result ? result->GetError() : "prepare failed");
    return false;
  }
  return true;
}
```

Update the two Task 1 deleters so phrases go first. In `delete_transcript` run `kDeletePhrasesByTranscriptIdQuery` with the id before `kDeleteTranscriptByIdQuery` (return false and log on error); in `delete_all_transcripts` run `kDeleteAllTranscriptPhrasesQuery` before `kDeleteAllTranscriptsQuery`.

- [ ] **Step 5: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R TranscriptDbTest --output-on-failure
```

Expected: 16 tests PASS (8 from Task 1, 8 new).

**Contingency, only if needed.** If DuckDB rejects `UPDATE Transcript ...` with a foreign-key constraint error once phrases exist (`StatusUpdateWorksOnTranscriptThatHasPhrases`, `UpdatePhraseTextMarksEditedAndBumpsTranscript`) or `update_event` fails for an event that has transcripts (`EditingAnEventKeepsItsTranscript`), do not work around it in the tests. Remove `REFERENCES Transcript(id)` from `TranscriptPhrase.transcript_id` (and, if the Event update is the one failing, `REFERENCES Event(id)` from `Transcript.event_id`) in `kCreateTables`, add a comment in the SQL — "No foreign key: DuckDB treats updates of referenced rows as key updates" — as the existing `ScheduleSeries` comment does, keep integrity in the `Database` methods (they already check parents exist or fail), and note the deviation in the commit message and in the spec (Task 4). `AddTranscriptRejectsInvalidAndUnknownEvent` and `AddPhraseRejectsInvalidInput` rely on the FK to reject unknown parents: without it, add an explicit existence check (`transcriptExists`, and an `EventExists` query `SELECT 1 FROM Event WHERE id = $1`) at the top of `add_transcript` / `add_transcript_phrase`.

- [ ] **Step 6: Commit**

```bash
git add src/database test/database_transcript_tests.cpp
git commit -m "Add transcript phrases: add, list, edit, delete (#118)"
```

---

### Task 3: Cascade deletes with events

**Files:**
- Modify: `src/database/constants.hpp`, `src/database/database.cpp`, `test/database_transcript_tests.cpp`

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: `Database::remove_event(id)` also deletes the event's transcripts and their phrases (before the event row, so no foreign key is violated); `Database::delete_event_series_overrides_from(series_id, from_ms)` deletes the transcripts and phrases of the events it deletes first. Client removal needs no change: `remove_client` only deletes a client that has no events (otherwise it deactivates it), so a deleted client has no transcripts.

- [ ] **Step 1: Write the failing tests**

Append to `test/database_transcript_tests.cpp`:

```cpp
TEST_F(TranscriptDbTest, RemoveEventDeletesItsTranscriptsAndPhrases) {
  const auto event_id = makeEvent();
  const auto other_id = makeEvent(1750000000000);
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  const auto other = db_->add_transcript(other_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "gone")), 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(other, "participant", "C", 0, 1000, "stays")), 0);

  ASSERT_TRUE(db_->remove_event(event_id));
  EXPECT_EQ(db_->get_event(event_id), nullptr);
  EXPECT_EQ(db_->get_transcript(t), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
  EXPECT_NE(db_->get_transcript(other), nullptr);
  EXPECT_EQ(db_->get_transcript_phrases(other).size(), 1u);
}

TEST_F(TranscriptDbTest, DeletingSeriesOverridesDeletesTheirTranscripts) {
  DuckEventSeries series;
  series.name = std::string{"Weekly"};
  series.start_date = 1730000000000;
  series.end_date = 1730003600000;
  series.event_stat_id = 1;
  series.payment_stat_id = 1;
  // Recurrence fields are optional for this test; see add_event_series callers
  // in test/database_tests.cpp for the minimal valid series if this insert fails.
  const auto series_id = db_->add_event_series(series);
  ASSERT_GT(series_id, 0);

  DuckEvent override_event;
  override_event.name = std::string{"Moved occurrence"};
  override_event.start_date = 1730100000000;
  override_event.end_date = 1730103600000;
  override_event.duration = 3600;
  override_event.event_stat_id = 1;
  override_event.payment_stat_id = 1;
  override_event.series_id = series_id;
  override_event.original_occurrence_start = 1730100000000;
  const auto event_id = db_->add_event(override_event);
  ASSERT_GT(event_id, 0);

  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "x")), 0);

  ASSERT_TRUE(db_->delete_event_series_overrides_from(series_id, 1730000000001));
  EXPECT_EQ(db_->get_event(event_id), nullptr);
  EXPECT_EQ(db_->get_transcript(t), nullptr);
  EXPECT_TRUE(db_->get_transcript_phrases(t).empty());
}
```

The series test may need a valid series; before writing the final version, read how `test/database_tests.cpp` builds a series (search `add_event_series`) and copy that minimal valid series; keep the assertions as written.

- [ ] **Step 2: Run to verify it fails**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R "RemoveEventDeletes|DeletingSeriesOverrides" --output-on-failure
```

Expected: FAIL — `remove_event` fails with a foreign-key violation (the transcript still references the event) and the override deletion likewise.

- [ ] **Step 3: Implement**

Queries in `constants.hpp`:

```cpp
constexpr auto kDeletePhrasesOfEventTranscriptsQuery = R"duckdb(
DELETE FROM TranscriptPhrase
WHERE transcript_id IN (SELECT id FROM Transcript WHERE event_id = $1)
)duckdb";
constexpr auto kDeleteTranscriptsOfEventQuery =
    "DELETE FROM Transcript WHERE event_id = $1";

constexpr auto kDeletePhrasesOfSeriesOverrideTranscriptsQuery = R"duckdb(
DELETE FROM TranscriptPhrase
WHERE transcript_id IN (
    SELECT t.id FROM Transcript t
    JOIN Event e ON e.id = t.event_id
    WHERE e.series_id = $1 AND e.original_occurrence_start >= $2)
)duckdb";
constexpr auto kDeleteTranscriptsOfSeriesOverridesQuery = R"duckdb(
DELETE FROM Transcript
WHERE event_id IN (
    SELECT id FROM Event
    WHERE series_id = $1 AND original_occurrence_start >= $2)
)duckdb";
```

In `Database::remove_event` (database.cpp), after the `EventChangeLog` deletion and before `kDeleteEventByIdQuery`, run the two event-level queries (phrases first, then transcripts) with `{duckdb::Value::BIGINT(id)}`; on error log the message and return false, in the same style as the neighbouring steps (no transcript text in logs).

In `Database::delete_event_series_overrides_from`, before the existing `kDeleteEventSeriesOverridesFromQuery`, run `kDeletePhrasesOfSeriesOverrideTranscriptsQuery` and `kDeleteTranscriptsOfSeriesOverridesQuery` with `{series_id, db_utils::toDuckTimestamp(occurrence_start_ms * 1000)}`; return false on error as the function already does.

If a transaction wraps these steps elsewhere (`mTxConn`), `write_connection(ownedConn)` already returns the transaction connection; use `conn` from it exactly as the neighbouring code does so the cascade is part of the same transaction.

- [ ] **Step 4: Run to verify it passes**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests Sessio_database_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R "TranscriptDbTest|DatabaseTest" --output-on-failure
```

Expected: PASS, including the existing `RemoveEventSucceedsAfterChangeLogRowsExist` and series tests.

- [ ] **Step 5: Commit**

```bash
git add src/database test/database_transcript_tests.cpp
git commit -m "Delete transcripts and phrases together with their event (#118)"
```

---

### Task 4: Migration, backup round trip, and spec corrections

**Files:**
- Modify: `test/database_transcript_tests.cpp`, `test/backup_tests.cpp`, `docs/superpowers/specs/2026-10-06-call-transcription-design.md`

**Interfaces:**
- Consumes: Tasks 1-3 and the existing backup helpers in `test/backup_tests.cpp` (`makeTestDatabase`, `removeIfExists`, `pcm::backup::BackupService`, `pcm::backup::RestoreService`).
- Produces: a test that a database created before this feature (no transcript tables) gains them on open; a test that a backup taken with transcripts restores them intact (AGENTS.md: schema changes need a migration and a round-trip test); corrected spec text.

- [ ] **Step 1: Write the migration test**

Append to `test/database_transcript_tests.cpp` (the fixture's `db_` is closed first; `duckdb.hpp` is available through `database.h`):

```cpp
#include <duckdb.hpp>

TEST_F(TranscriptDbTest, OpeningADatabaseWithoutTranscriptTablesCreatesThem) {
  // Simulate a database created by an older version: remove the new tables.
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db");
    duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS TranscriptPhrase")->HasError());
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS Transcript")->HasError());
  }

  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);

  const auto event_id = makeEvent();
  const auto t = db_->add_transcript(event_id, "live_local_v1");
  ASSERT_GT(t, 0);
  ASSERT_GT(db_->add_transcript_phrase(phrase(t, "participant", "C", 0, 1000, "works")), 0);
  EXPECT_EQ(db_->get_transcript_phrases(t).size(), 1u);
}

TEST_F(TranscriptDbTest, ExistingDataSurvivesTheTranscriptMigration) {
  const auto event_id = makeEvent();
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db");
    duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS TranscriptPhrase")->HasError());
    ASSERT_FALSE(conn.Query("DROP TABLE IF EXISTS Transcript")->HasError());
  }
  pcm::config::Config conf{
      .db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  EXPECT_NE(db_->get_event(event_id), nullptr);
}

TEST_F(TranscriptDbTest, SchemaVersionStaysOne) {
  EXPECT_EQ(db_->get_application_metadata().schema_version, 1);
}
```

Put the `#include <duckdb.hpp>` at the top of the file with the other includes (shown inline here only for context).

- [ ] **Step 2: Write the restore round-trip test**

Add to `test/backup_tests.cpp` after `RestoresDatabaseSnapshotIntoNewDirectory`, copying that test's structure and cleanup style (use unique temp names `tmp_restore_transcript_source`, `tmp_restore_transcript.psybackup`, `tmp_restore_transcript_target`):

```cpp
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
  ASSERT_GT(sourceDb.add_transcript_phrase(phrase), 0);
  ASSERT_TRUE(sourceDb.set_transcript_status(transcriptId, "draft"));
  ASSERT_TRUE(sourceDb.revoke_transcript_consent(transcriptId, 1730000300000));

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
  ASSERT_EQ(phrases.size(), 1u);
  EXPECT_EQ(phrases[0].text, "Привет, это тест восстановления");
  EXPECT_EQ(phrases[0].start_ms, 1500);
  EXPECT_EQ(phrases[0].end_ms, 4200);
  EXPECT_EQ(phrases[0].speaker_name.value_or(""), "Client");

  Poco::File(backupPath).remove();
  Poco::File(targetPath).remove(true);
  Poco::File(Poco::Path(Poco::Path::current()).append("tmp_restore_transcript_source"))
      .remove(true);
}
```

If `makeTestDatabase` or `removeIfExists` are defined later in the file than the insertion point, insert the test after their definitions (they are in the file's anonymous namespace near the top).

Also add a case for an encrypted backup only if cheap: reuse `createEncryptedBackup` / `restoreWithRecoveryPhrase` the same way as `RestoresEncryptedBackupWithCorrectPassword`, with the same transcript assertions on `transcripts.size() == 1` and the phrase text. If it duplicates more than ~25 lines, skip it: encrypted backups wrap the same exported directory.

- [ ] **Step 3: Run**

```bash
distrobox-host-exec cmake --build build-tr --target Sessio_database_transcript_tests Sessio_backup_tests --parallel 3
distrobox-host-exec ctest --test-dir build-tr -R "TranscriptDbTest|RestoreServiceTest" --output-on-failure
```

Expected: PASS. A failure of the restore test with "table not found" or an empty result means `IMPORT DATABASE` skipped the new tables: check that `build-tr` reconfigured (the backup target links `Sessio_database`) and read `schema.sql`/`load.sql` of an exported snapshot for the two tables.

- [ ] **Step 4: Correct the spec**

In `docs/superpowers/specs/2026-10-06-call-transcription-design.md`, section "Data model and consent":
- Replace the sentence about raising `schema_version` ("The schema change is added to `kCreateTables` and `kSchemaMigrations`, raises `schema_version`, and is covered by a restore round-trip test, as AGENTS.md requires.") with: "The tables are added to `kCreateTables` (`CREATE TABLE IF NOT EXISTS`, applied on every open, so existing databases migrate on first start); `schema_version` stays 1, as for every schema addition so far, because restore rejects other values. A migration test and a restore round-trip test cover it, as AGENTS.md requires."
- In the Architecture table, replace the `TranscriptRepository` row's text with: "Methods on `pcm::database::Database` (`add_transcript`, `add_transcript_phrase`, ...), following the existing convention; no separate class".
- In Lifecycle step 6, replace "Deleting an event or a client deletes its transcripts and phrases." with "Deleting an event (including future series overrides) deletes its transcripts and phrases; a client that has events is only deactivated, so its transcripts stay until the event is deleted."
- If the Task 2 contingency was used, add a sentence that the foreign keys were dropped and why.

- [ ] **Step 5: Commit**

```bash
git add test docs/superpowers/specs
git commit -m "Test transcript migration and backup round trip; correct the data-model spec (#118)"
```

---

## Self-Review

**Spec coverage (phase 2: "Schema, repository, migration, backup and restore, cascade deletes").**
- Schema: Task 1 (tables in `kCreateTables`, structs).
- Repository: Tasks 1-2 (`Database` methods; the spec's separate class is dropped, recorded in Task 4).
- Migration: Task 4 (drop-and-reopen test, schema version stays 1, spec corrected).
- Backup and restore: Task 4 (round-trip test; backups use `EXPORT DATABASE` which includes new tables automatically, encrypted backups wrap the same directory).
- Cascade deletes: Task 3 (event removal and series override deletion; client removal needs none because clients with events are only deactivated).
- Crash recovery from the lifecycle ("a crash does not lose text"): `finalize_interrupted_transcripts` (Task 1); wiring it into application start belongs to phase 3.
- Settings "Delete all transcripts": `delete_all_transcripts` (Tasks 1-2); the button is phase 4.
- Not in this plan: the `TranscriptionSession` that writes phrases as they are final (phase 3), UI and settings (phase 4), version bump, CHANGELOG and licence notices (phase 5).

**Placeholder scan.** No TBD/TODO. Two spots tell the engineer to read an existing test for a minimal valid series (Task 3) and a helper's position (Task 4); each names where to look and what to copy, and the assertions are fixed.

**Type consistency.** `DuckTranscript` / `DuckTranscriptPhrase` field names and the `Database` signatures are identical across tasks and the Public API block; ids are `int64_t` in the API and read via `GetValue<int32_t>()` from `INTEGER` columns; times are epoch milliseconds in the API and `TIMESTAMP` in the table (`consent_*`, `created_at`, `updated_at`) except phrase offsets, which are plain `BIGINT` milliseconds.

## Execution

Next plans (written when reached): phase 3 session and consent (`AudioSink`, taps, `TranscriptionSession`, `VideoSession` integration; also fix the local `Sessio` link before it), phase 4 interface, phase 5 release.
