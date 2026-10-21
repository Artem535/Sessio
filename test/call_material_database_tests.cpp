// Included by database_transcript_tests.cpp to use its temporary DB fixture.

TEST_F(TranscriptDbTest, CallMaterialManualOnlySurvivesRestartAndIsListedWithoutAssociations) {
  const auto first = db_->create_call_material(std::nullopt);
  const auto second = db_->create_call_material(std::nullopt);
  ASSERT_GT(first, 0);
  ASSERT_GT(second, first);
  EXPECT_EQ(db_->get_call_material(first).revision, 0);
  EXPECT_EQ(db_->get_call_material(first).json, "{}");
  const std::string json = R"({"manual_notes":"Только мои заметки\nSecond line"})";
  ASSERT_TRUE(db_->save_call_material(first, 0, json));
  EXPECT_FALSE(db_->save_call_material(first, 0, "{}"));
  const auto created = db_->get_call_material(first).created_at;
  EXPECT_GT(created, 1700000000000);
  db_.reset();
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  const auto calls = db_->get_call_materials();
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[0].id, second);
  EXPECT_EQ(calls[1].id, first);
  EXPECT_EQ(calls[1].created_at, created);
  EXPECT_EQ(calls[1].revision, 1);
  EXPECT_EQ(calls[1].json, json);
  EXPECT_FALSE(calls[1].event_id.has_value());
  EXPECT_TRUE(db_->get_call_material_transcript_ids(first).empty());
  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_EQ(db_->get_call_material(first).json, json);
}

TEST_F(TranscriptDbTest, CallMaterialLinksStopRestartSegmentsWithoutMergingDifferentCallsAtSameEvent) {
  const auto event = makeEvent();
  const auto first = db_->create_call_material(event);
  const auto second = db_->create_call_material(event);
  const auto t1 = db_->add_transcript(event, "scope");
  const auto t2 = db_->add_transcript(event, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(first, t1));
  const auto revision = db_->get_call_material(first).revision;
  ASSERT_TRUE(db_->link_call_material_transcript(first, t1));
  EXPECT_EQ(db_->get_call_material(first).revision, revision);
  ASSERT_TRUE(db_->link_call_material_transcript(first, t2));
  EXPECT_EQ(db_->get_call_material_transcript_ids(first), (std::vector<int64_t>{t1, t2}));
  EXPECT_EQ(db_->get_call_material_for_transcript(t2).id, first);
  EXPECT_FALSE(db_->link_call_material_transcript(second, t1));
  EXPECT_TRUE(db_->get_call_material_transcript_ids(second).empty());
  ASSERT_TRUE(db_->remove_event(event));
  EXPECT_FALSE(db_->get_call_material(first).event_id.has_value());
  EXPECT_FALSE(db_->get_call_material(second).event_id.has_value());
  EXPECT_EQ(db_->get_call_material_for_transcript(t1).id, first);
}

TEST_F(TranscriptDbTest, CallMaterialRejectsInvalidInputsAndMissingRows) {
  EXPECT_EQ(db_->create_call_material(-1), 0);
  EXPECT_EQ(db_->create_call_material(999999), 0);
  EXPECT_EQ(db_->get_call_material(999999).id, 0);
  EXPECT_EQ(db_->get_call_material_for_transcript(999999).id, 0);
  const auto id = db_->create_call_material(std::nullopt);
  EXPECT_FALSE(db_->save_call_material(id, -1, "{}"));
  EXPECT_FALSE(db_->save_call_material(id, 0, "broken"));
  EXPECT_FALSE(db_->save_call_material(id, 0, "[]"));
  EXPECT_FALSE(db_->save_call_material(id, 0, R"({"manual_notes":42})"));
  EXPECT_FALSE(db_->save_call_material(id, 0, R"({"manual_entries":"not an array"})"));
  EXPECT_FALSE(db_->save_call_material(id, 0, std::string(4 * 1024 * 1024 + 1, 'x')));
  EXPECT_FALSE(db_->save_call_material(999999, 0, "{}"));
  const auto t = db_->add_transcript(std::nullopt, "scope");
  EXPECT_FALSE(db_->link_call_material_transcript(id, 999999));
  EXPECT_FALSE(db_->link_call_material_transcript(999999, t));
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_FALSE(db_->link_call_material_transcript(id, t));
  EXPECT_EQ(db_->get_call_material(id).revision, 0);
}

TEST_F(TranscriptDbTest, CallMaterialAdoptsLateEventRejectsMismatchAndPreservesEventForStandaloneSegment) {
  const auto firstEvent = makeEvent();
  const auto otherEvent = makeEvent(1740000000000);
  const auto material = db_->create_call_material(std::nullopt);
  const auto scheduled = db_->add_transcript(firstEvent, "scope");
  const auto other = db_->add_transcript(otherEvent, "scope");
  const auto standalone = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(material, scheduled));
  EXPECT_EQ(db_->get_call_material(material).event_id, firstEvent);
  EXPECT_FALSE(db_->link_call_material_transcript(material, other));
  EXPECT_EQ(db_->get_call_material_for_transcript(other).id, 0);
  ASSERT_TRUE(db_->link_call_material_transcript(material, standalone));
  EXPECT_EQ(db_->get_call_material(material).event_id, firstEvent);

  // A repeat link sees a transcript event assigned after the first link.
  const auto late = db_->create_call_material(std::nullopt);
  const auto t = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(late, t));
  const auto revision = db_->get_call_material(late).revision;
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("UPDATE Transcript SET event_id=" + std::to_string(firstEvent) +
                           " WHERE id=" + std::to_string(t))->HasError());
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  ASSERT_TRUE(db_->link_call_material_transcript(late, t));
  EXPECT_EQ(db_->get_call_material(late).event_id, firstEvent);
  EXPECT_EQ(db_->get_call_material(late).revision, revision);
}

TEST_F(TranscriptDbTest, CallMaterialInvalidationPreservesLifecycleAndClearsPacketsAndSnapshots) {
  const auto id = db_->create_call_material(std::nullopt);
  const auto t = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(id, t));
  ASSERT_TRUE(db_->save_call_material(id, db_->get_call_material(id).revision,
      R"({"manual_text":"Keep","ended":true,"live_status":"ready","final_status":"running","live":{"sources":["Sensitive"]},"final":{"text":"Sensitive"}})"));
  const auto revision = db_->get_call_material(id).revision;
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->get_call_material(id).json,
      R"({"manual_text":"Keep","ended":true,"live_status":"stale","final_status":"stale"})");
  EXPECT_FALSE(db_->save_call_material(id, revision, R"({"final":{"text":"Late result"}})"));
}

TEST_F(TranscriptDbTest, CallMaterialFirstTranscriptLinkDoesNotInventGeneratedStatuses) {
  const struct {
    const char *stored;
    const char *expected;
  } cases[] = {
      {"{}", "{}"},
      {R"({"live_status":null,"final_status":null})", "{}"},
      {R"({"live_status":"idle","final_status":"idle"})",
       R"({"live_status":"idle","final_status":"idle"})"},
      {R"({"manual_text":"Keep","ended":false,"live_status":"ready","final_status":null})",
       R"({"manual_text":"Keep","ended":false,"live_status":"stale"})"},
      {R"({"live_status":null,"final_status":"running"})", R"({"final_status":"stale"})"},
      {R"({"live_status":"","final_status":""})", "{}"},
  };
  for (const auto &example : cases) {
    SCOPED_TRACE(example.stored);
    const auto id = db_->create_call_material(std::nullopt);
    const auto t = db_->add_transcript(std::nullopt, "scope");
    ASSERT_TRUE(db_->save_call_material(id, 0, example.stored));
    ASSERT_TRUE(db_->link_call_material_transcript(id, t));
    EXPECT_EQ(db_->get_call_material(id).json, example.expected);
    EXPECT_EQ(db_->get_call_material(id).revision, 2);
  }
}

TEST_F(TranscriptDbTest, CallMaterialSourceChangesClearDerivedContentPreserveManualAndRejectLateSaves) {
  const auto id = db_->create_call_material(std::nullopt);
  const auto t = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(id, t));
  DuckTranscriptPhrase p;
  p.transcript_id = t; p.track_role = "participant"; p.text = "Before"; p.end_ms = 100;
  const auto phrase = db_->add_transcript_phrase(p);
  ASSERT_GT(phrase, 0);
  const auto other = db_->create_call_material(std::nullopt);
  const std::string manual = R"({"manual_notes":"Keep","manual_text":"Also keep"})";
  const std::string derived = R"({"manual_notes":"Keep","manual_text":"Also keep","final":{"text":"Sensitive"},"key_points":["Sensitive"]})";
  ASSERT_TRUE(db_->save_call_material(other, 0, derived));
  auto seed = [&] {
    const auto state = db_->get_call_material(id);
    EXPECT_TRUE(db_->save_call_material(id, state.revision, derived));
    return state.revision + 1;
  };
  auto revision = seed();
  ASSERT_TRUE(db_->update_transcript_phrase_text(phrase, "After"));
  EXPECT_EQ(db_->get_call_material(id).json, manual);
  EXPECT_FALSE(db_->save_call_material(id, revision, derived));
  revision = seed();
  ASSERT_TRUE(db_->rename_transcript_speaker(t, "participant", "New name"));
  EXPECT_EQ(db_->get_call_material(id).json, manual);
  EXPECT_FALSE(db_->save_call_material(id, revision, derived));
  revision = seed();
  ASSERT_TRUE(db_->delete_transcript_phrase(phrase));
  EXPECT_EQ(db_->get_call_material(id).json, manual);
  EXPECT_FALSE(db_->save_call_material(id, revision, derived));
  revision = seed();
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->get_call_material(id).json, manual);
  EXPECT_FALSE(db_->save_call_material(id, revision, derived));
  EXPECT_EQ(db_->get_call_material(other).json, derived);
}
TEST_F(TranscriptDbTest, CallMaterialInvalidationPreservesSavedEntriesAndTheirAttachments) {
  const auto id = db_->create_call_material(std::nullopt);
  const auto t = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(id, t));
  const std::string state = R"({"manual_entries":[{"id":"entry","body_markdown":"**Keep**","attachments":[{"relative_path":"calls/1/file"}]}],"final":{"text":"Derived"}})";
  ASSERT_TRUE(db_->save_call_material(id, db_->get_call_material(id).revision, state));
  ASSERT_TRUE(db_->revoke_transcript_consent(t));
  EXPECT_EQ(db_->get_call_material(id).json,
            R"({"manual_entries":[{"id":"entry","body_markdown":"**Keep**","attachments":[{"relative_path":"calls/1/file"}]}]})");
}

TEST_F(TranscriptDbTest, CallMaterialDeletingTranscriptPreservesManualNotesAndOtherSegments) {
  const auto id = db_->create_call_material(std::nullopt);
  const auto unrelated = db_->create_call_material(std::nullopt);
  const auto t1 = db_->add_transcript(std::nullopt, "scope");
  const auto t2 = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(id, t1));
  ASSERT_TRUE(db_->link_call_material_transcript(id, t2));
  const auto revision = db_->get_call_material(id).revision;
  ASSERT_TRUE(db_->save_call_material(id, revision,
      R"({"manual_notes":"Call","ended":true,"final_status":"ready","final":"derived"})"));
  ASSERT_TRUE(db_->save_call_material(unrelated, 0, R"({"manual_notes":"Unrelated"})"));
  ASSERT_TRUE(db_->delete_transcript(t1));
  EXPECT_EQ(db_->get_call_material(id).id, id);
  EXPECT_EQ(db_->get_call_material(id).json,
      R"({"manual_notes":"Call","ended":true,"final_status":"stale"})");
  EXPECT_EQ(db_->get_call_material(id).revision, revision + 2);
  EXPECT_EQ(db_->get_call_material_for_transcript(t1).id, 0);
  EXPECT_EQ(db_->get_call_material_for_transcript(t2).id, id);
  EXPECT_EQ(db_->get_call_material_transcript_ids(id), std::vector<int64_t>{t2});
  EXPECT_FALSE(db_->save_call_material(id, revision + 1, "{}"));
  EXPECT_NE(db_->get_transcript(t2), nullptr);
  EXPECT_EQ(db_->get_call_material(unrelated).json, R"({"manual_notes":"Unrelated"})");
  ASSERT_TRUE(db_->delete_transcript(t2));
  EXPECT_EQ(db_->get_call_material(id).id, id);
  EXPECT_TRUE(db_->get_call_material_transcript_ids(id).empty());
  db_.reset();
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  EXPECT_EQ(db_->get_call_material(id).json,
      R"({"manual_notes":"Call","ended":true,"final_status":"stale"})");
  EXPECT_GT(db_->create_call_material(std::nullopt), unrelated);
}

TEST_F(TranscriptDbTest, CallMaterialDeletingAllTranscriptsPreservesEveryManualNoteAndInvalidatesLinkedCallsOnce) {
  const auto first = db_->create_call_material(std::nullopt);
  const auto second = db_->create_call_material(std::nullopt);
  const auto standalone = db_->create_call_material(std::nullopt);
  const auto t1 = db_->add_transcript(std::nullopt, "scope");
  const auto t2 = db_->add_transcript(std::nullopt, "scope");
  const auto t3 = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(first, t1));
  ASSERT_TRUE(db_->link_call_material_transcript(first, t2));
  ASSERT_TRUE(db_->link_call_material_transcript(second, t3));
  const std::string saved = R"({"manual_text":"Keep","ended":true,"live_status":"ready","final_status":"ready","live":{"sources":["Sensitive"]},"final":{"text":"Sensitive"}})";
  ASSERT_TRUE(db_->save_call_material(first, db_->get_call_material(first).revision, saved));
  ASSERT_TRUE(db_->save_call_material(second, db_->get_call_material(second).revision, saved));
  ASSERT_TRUE(db_->save_call_material(standalone, 0, R"({"manual_text":"Independent"})"));
  const auto firstRevision = db_->get_call_material(first).revision;
  const auto secondRevision = db_->get_call_material(second).revision;
  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_EQ(db_->count_transcripts(), 0);
  EXPECT_EQ(db_->get_call_materials().size(), 3u);
  for (const auto id : {first, second}) {
    EXPECT_EQ(db_->get_call_material(id).json,
        R"({"manual_text":"Keep","ended":true,"live_status":"stale","final_status":"stale"})");
    EXPECT_TRUE(db_->get_call_material_transcript_ids(id).empty());
  }
  EXPECT_EQ(db_->get_call_material(first).revision, firstRevision + 1);
  EXPECT_EQ(db_->get_call_material(second).revision, secondRevision + 1);
  EXPECT_FALSE(db_->save_call_material(first, firstRevision, saved));
  EXPECT_FALSE(db_->save_call_material(second, secondRevision, saved));
  EXPECT_EQ(db_->get_call_material(standalone).json, R"({"manual_text":"Independent"})");
  EXPECT_EQ(db_->get_call_material(standalone).revision, 1);
  ASSERT_TRUE(db_->delete_all_transcripts());
  EXPECT_EQ(db_->get_call_material(first).revision, firstRevision + 1);
}

TEST_F(TranscriptDbTest, CallMaterialSchemaThreeUpgradeIsIdempotentAndPreservesOldPrompterState) {
  const auto t = db_->add_transcript(std::nullopt, "scope");
  const std::string old = R"({"cards":[{"id":"old-card"}]})";
  ASSERT_TRUE(db_->save_prompter_state(t, 0, old));
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE CallMaterialTranscript; DROP TABLE CallMaterial; "
                            "DROP SEQUENCE call_material_id_seq; UPDATE ApplicationMetadata SET "
                            "schema_version=3,last_migration_at=TIMESTAMP '2000-01-01'")->HasError());
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  const auto metadata = db_->get_application_metadata();
  EXPECT_EQ(metadata.schema_version, 4);
  EXPECT_GT(metadata.last_migration_at, 1700000000000);
  EXPECT_EQ(db_->get_prompter_state(t).json, old);
  EXPECT_TRUE(db_->get_call_materials().empty());
  const auto id = db_->create_call_material(std::nullopt);
  ASSERT_GT(id, 0);
  db_.reset(); db_ = std::make_unique<pcm::database::Database>(conf);
  EXPECT_EQ(db_->get_application_metadata().last_migration_at, metadata.last_migration_at);
  EXPECT_EQ(db_->get_call_material(id).id, id);
}

TEST_F(TranscriptDbTest, CallMaterialFailedTranscriptDeletionRollsBackInvalidationAndDetachment) {
  const auto t = db_->add_transcript(std::nullopt, "scope");
  const auto id = db_->create_call_material(std::nullopt);
  ASSERT_TRUE(db_->link_call_material_transcript(id, t));
  ASSERT_TRUE(db_->save_call_material(id, db_->get_call_material(id).revision,
      R"({"manual_text":"Keep","final":{"text":"Derived"}})"));
  const auto before = db_->get_call_material(id);
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("CREATE TABLE MaterialDeletionGuard (transcript_id INTEGER REFERENCES Transcript(id));"
                           "INSERT INTO MaterialDeletionGuard VALUES (" + std::to_string(t) + ")")->HasError());
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  EXPECT_FALSE(db_->delete_transcript(t));
  EXPECT_EQ(db_->get_call_material(id).revision, before.revision);
  EXPECT_EQ(db_->get_call_material(id).json, before.json);
  EXPECT_EQ(db_->get_call_material_for_transcript(t).id, id);
}

TEST_F(TranscriptDbTest, CallMaterialFailedMigrationDoesNotAdvanceSchemaOrLeavePartialTables) {
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DROP TABLE CallMaterialTranscript; DROP TABLE CallMaterial; "
                            "DROP SEQUENCE call_material_id_seq; CREATE TABLE CallMaterial(broken INTEGER); "
                            "UPDATE ApplicationMetadata SET schema_version=3")->HasError());
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  EXPECT_THROW(db_ = std::make_unique<pcm::database::Database>(conf), std::runtime_error);
  duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
  EXPECT_EQ(conn.Query("SELECT schema_version FROM ApplicationMetadata")->Fetch()->GetValue(0, 0).GetValue<int32_t>(), 3);
  EXPECT_TRUE(conn.Query("SELECT * FROM CallMaterialTranscript")->HasError());
}

TEST_F(TranscriptDbTest, CallMaterialCleanupPreservesManualNotesAndOtherSegmentsAndDetachesMissingEvent) {
  const auto event = makeEvent();
  const auto standalone = db_->create_call_material(event);
  const auto doomed = db_->create_call_material(std::nullopt);
  const auto t = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(doomed, t));
  const auto retained = db_->add_transcript(std::nullopt, "scope");
  ASSERT_TRUE(db_->link_call_material_transcript(doomed, retained));
  ASSERT_TRUE(db_->save_call_material(doomed, db_->get_call_material(doomed).revision,
      R"({"manual_text":"Keep","final":{"text":"Derived"}})"));
  db_.reset();
  {
    duckdb::DuckDB raw(dir_ + "/database.db"); duckdb::Connection conn(raw);
    ASSERT_FALSE(conn.Query("DELETE FROM Transcript WHERE id=" + std::to_string(t) +
                           "; DELETE FROM Event WHERE id=" + std::to_string(event))->HasError());
  }
  pcm::config::Config conf{.db_conf = pcm::config::DatabaseConfig{.db_pth = Poco::Path(dir_)}};
  db_ = std::make_unique<pcm::database::Database>(conf);
  db_->purge_orphan_transcripts();
  EXPECT_EQ(db_->get_call_material(doomed).id, doomed);
  EXPECT_EQ(db_->get_call_material(doomed).json, R"({"manual_text":"Keep"})");
  EXPECT_EQ(db_->get_call_material_transcript_ids(doomed), std::vector<int64_t>{retained});
  EXPECT_EQ(db_->get_call_material(standalone).id, standalone);
  EXPECT_FALSE(db_->get_call_material(standalone).event_id.has_value());
}

TEST_F(TranscriptDbTest, CallMaterialsForClientIncludeEventAndTranscriptLinks) {
  DuckClient c1, c2;
  c1.name = std::string{"First"};
  c2.name = std::string{"Second"};
  const auto client = db_->add_client(c1), other = db_->add_client(c2);
  ASSERT_GT(client, 0);
  ASSERT_GT(other, 0);
  const auto event = makeEvent();
  ASSERT_GT(db_->add_event_client(event, client), 0);
  const auto byEvent = db_->create_call_material(event);
  const auto byTranscript = db_->create_call_material(std::nullopt);
  const auto transcript = db_->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(byTranscript, transcript));
  ASSERT_TRUE(db_->set_transcript_status(transcript, "draft"));
  ASSERT_TRUE(db_->set_transcript_clients(transcript, {client}));
  const auto both = db_->create_call_material(event);
  const auto t2 = db_->add_transcript(event, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(both, t2));
  ASSERT_TRUE(db_->set_transcript_status(t2, "draft"));
  ASSERT_TRUE(db_->set_transcript_clients(t2, {client}));
  db_->create_call_material(std::nullopt);  // unrelated
  const auto foreign = db_->create_call_material(std::nullopt);
  const auto t3 = db_->add_transcript(std::nullopt, "live_local_v1");
  ASSERT_TRUE(db_->link_call_material_transcript(foreign, t3));
  ASSERT_TRUE(db_->set_transcript_status(t3, "draft"));
  ASSERT_TRUE(db_->set_transcript_clients(t3, {other}));

  const auto found = db_->get_call_materials_for_client(client);
  std::vector<int64_t> ids;
  for (const auto &m : found) ids.push_back(m.id);
  EXPECT_EQ(ids, (std::vector<int64_t>{both, byTranscript, byEvent}));
  EXPECT_TRUE(db_->get_call_materials_for_client(0).empty());
}
