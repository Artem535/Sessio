#include "index_store.h"

#include "lbug.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace sessio::rag {

namespace {

using lbug::main::Connection;
using lbug::main::Database;

lbug::common::Value floatArray(const std::vector<float> &vec) {
  std::vector<std::unique_ptr<lbug::common::Value>> children;
  children.reserve(vec.size());
  for (float v : vec) children.push_back(std::make_unique<lbug::common::Value>(v));
  return lbug::common::Value(
      lbug::common::LogicalType::ARRAY(lbug::common::LogicalType::FLOAT(), vec.size()), std::move(children));
}

bool exec(Connection &conn, const std::string &query, std::string *error) {
  auto result = conn.query(query);
  if (!result->isSuccess()) {
    if (error) *error = result->getErrorMessage();
    return false;
  }
  return true;
}

bool hasIndex(Connection &conn, const std::string &name) {
  auto result = conn.query("CALL SHOW_INDEXES() RETURN table_name, index_name");
  if (!result->isSuccess()) return false;
  while (result->hasNext()) {
    const auto row = result->getNext();
    if (row->getValue(1)->getValue<std::string>() == name) return true;
  }
  return false;
}

// lbug refuses non-UTF-8 strings outright. ASR/LLM text occasionally carries a
// broken byte; replace every invalid sequence with a space so one bad byte in a
// query cannot abort the whole search.
std::string sanitizeUtf8(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    size_t length = 0;
    if ((c & 0xE0) == 0xC0) length = 2;
    else if ((c & 0xF0) == 0xE0) length = 3;
    else if ((c & 0xF8) == 0xF0) length = 4;
    bool valid = length != 0 && i + length <= text.size();
    if (valid) {
      for (size_t j = 1; j < length; ++j)
        if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) {
          valid = false;
          break;
        }
    }
    if (valid) {
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      if (length == 2 && c < 0xC2) valid = false;               // overlong
      else if (length == 3) {
        if (c == 0xE0 && c1 < 0xA0) valid = false;              // overlong
        else if (c == 0xED && c1 >= 0xA0) valid = false;        // surrogate
      } else if (length == 4) {
        if (c > 0xF4) valid = false;
        else if (c == 0xF0 && c1 < 0x90) valid = false;         // overlong
        else if (c == 0xF4 && c1 >= 0x90) valid = false;        // > U+10FFFF
      }
    }
    if (valid) {
      out.append(text, i, length);
      i += length;
    } else {
      out.push_back(' ');
      ++i;
    }
  }
  return out;
}

} // namespace

struct IndexStore::Impl {
  Database db;
  Connection conn;
  bool valid = false;

  Impl(const std::string &dbPath, const std::string &extensionDir, std::string *error)
      : db(dbPath), conn(&db) {
    try {
      if (!exec(conn, "LOAD EXTENSION '" + extensionDir + "/fts/libfts.lbug_extension'", error) ||
          !exec(conn, "LOAD EXTENSION '" + extensionDir + "/vector/libvector.lbug_extension'", error) ||
          !exec(conn,
                "CREATE NODE TABLE IF NOT EXISTS Document(id STRING PRIMARY KEY, title STRING, "
                "url STRING, collection STRING, revision INT64, doc_type STRING, primary_entity STRING)",
                error) ||
          !exec(conn,
                "CREATE NODE TABLE IF NOT EXISTS Chunk(id SERIAL PRIMARY KEY, docId STRING, "
                "text STRING, embedding FLOAT[" + std::to_string(kEmbeddingDim) + "])",
                error) ||
          !exec(conn,
                "CREATE NODE TABLE IF NOT EXISTS Entity(name STRING PRIMARY KEY, kind STRING, "
                "docs STRING, primary_docs STRING)",
                error) ||
          !exec(conn,
                "CREATE NODE TABLE IF NOT EXISTS Faq(id SERIAL PRIMARY KEY, docId STRING, "
                "question STRING, answer STRING, url STRING, embedding FLOAT["
                    + std::to_string(kEmbeddingDim) + "])",
                error) ||
          !exec(conn,
                "CREATE NODE TABLE IF NOT EXISTS Passport(name STRING PRIMARY KEY, industry STRING, "
                "client_type STRING, what_built STRING, deployment STRING, description STRING, "
                "docId STRING, url STRING, aliases STRING, models STRING, embedding FLOAT["
                    + std::to_string(kEmbeddingDim) + "])",
                error))
        return;
      if (!hasIndex(conn, "faq_vec") &&
          !exec(conn, "CALL CREATE_VECTOR_INDEX('Faq','faq_vec','embedding',metric := 'cosine')", error))
        return;
      if (!hasIndex(conn, "chunk_fts") &&
          !exec(conn, "CALL CREATE_FTS_INDEX('Chunk','chunk_fts',['text'],stemmer := 'russian')", error))
        return;
      if (!hasIndex(conn, "chunk_vec") &&
          !exec(conn, "CALL CREATE_VECTOR_INDEX('Chunk','chunk_vec','embedding',metric := 'cosine')", error))
        return;
      valid = true;
    } catch (const std::exception &e) {
      if (error) *error = e.what();
    }
  }
};

IndexStore::IndexStore(const std::string &dbPath, const std::string &extensionDir, std::string *error)
    : mImpl(std::make_unique<Impl>(dbPath, extensionDir, error)) {}

IndexStore::~IndexStore() = default;

bool IndexStore::ok() const { return mImpl && mImpl->valid; }

bool IndexStore::upsertDocument(const DocumentInfo &doc, const std::vector<IndexedChunk> &chunks,
                                std::string *error) {
  if (!ok()) return false;
  auto &conn = mImpl->conn;
  if (!exec(conn, "BEGIN TRANSACTION", error)) return false;
  const auto rollback = [&] {
    std::string ignored;
    exec(conn, "ROLLBACK", &ignored);
  };
  auto deleteOld = conn.prepare("MATCH (c:Chunk {docId:$docId}) DELETE c");
  auto deleted = conn.execute(deleteOld.get(), std::make_pair(std::string("docId"), doc.docId));
  if (!deleted->isSuccess()) {
    if (error) *error = deleted->getErrorMessage();
    rollback();
    return false;
  }
  auto mergeDoc = conn.prepare(
      "MERGE (d:Document {id:$id}) SET d.title=$title, d.url=$url, d.collection=$collection, "
      "d.revision=$revision, d.doc_type=$docType, d.primary_entity=$primaryEntity");
  auto merged = conn.execute(mergeDoc.get(), std::make_pair(std::string("id"), doc.docId),
                             std::make_pair(std::string("title"), doc.title),
                             std::make_pair(std::string("url"), doc.url),
                             std::make_pair(std::string("collection"), doc.collection),
                             std::make_pair(std::string("revision"), static_cast<int64_t>(std::stoll(doc.revision))),
                             std::make_pair(std::string("docType"), doc.docType),
                             std::make_pair(std::string("primaryEntity"), doc.primary));
  if (!merged->isSuccess()) {
    if (error) *error = merged->getErrorMessage();
    rollback();
    return false;
  }
  auto insert = conn.prepare("CREATE (c:Chunk {docId:$docId, text:$text, embedding:$emb})");
  for (const auto &chunk : chunks) {
    if (chunk.embedding.size() != static_cast<size_t>(kEmbeddingDim)) {
      if (error) *error = "embedding dimension mismatch";
      rollback();
      return false;
    }
    auto inserted = conn.execute(insert.get(), std::make_pair(std::string("docId"), doc.docId),
                                 std::make_pair(std::string("text"), chunk.text),
                                 std::make_pair(std::string("emb"), floatArray(chunk.embedding)));
    if (!inserted->isSuccess()) {
      if (error) *error = inserted->getErrorMessage();
      rollback();
      return false;
    }
  }
  if (!exec(conn, "COMMIT", error)) {
    rollback();
    return false;
  }
  return true;
}

bool IndexStore::removeDocument(const std::string &docId, std::string *error) {
  if (!ok()) return false;
  auto &conn = mImpl->conn;
  if (!exec(conn, "BEGIN TRANSACTION", error)) return false;
  auto deleteChunks = conn.prepare("MATCH (c:Chunk {docId:$docId}) DELETE c");
  auto chunks = conn.execute(deleteChunks.get(), std::make_pair(std::string("docId"), docId));
  auto deleteDoc = conn.prepare("MATCH (d:Document {id:$id}) DELETE d");
  auto doc = conn.execute(deleteDoc.get(), std::make_pair(std::string("id"), docId));
  if (!chunks->isSuccess() || !doc->isSuccess()) {
    if (error) *error = !chunks->isSuccess() ? chunks->getErrorMessage() : doc->getErrorMessage();
    std::string ignored;
    exec(conn, "ROLLBACK", &ignored);
    return false;
  }
  return exec(conn, "COMMIT", error);
}

std::optional<int64_t> IndexStore::documentRevision(const std::string &docId) {
  if (!ok()) return std::nullopt;
  auto prepared = mImpl->conn.prepare("MATCH (d:Document {id:$id}) RETURN d.revision");
  auto result = mImpl->conn.execute(prepared.get(), std::make_pair(std::string("id"), docId));
  if (!result->isSuccess() || !result->hasNext()) return std::nullopt;
  return result->getNext()->getValue(0)->getValue<int64_t>();
}

std::optional<DocumentInfo> IndexStore::documentInfo(const std::string &docId) {
  if (!ok()) return std::nullopt;
  auto prepared = mImpl->conn.prepare(
      "MATCH (d:Document {id:$id}) RETURN d.title, d.url, d.collection, d.revision, d.doc_type, d.primary_entity");
  auto result = mImpl->conn.execute(prepared.get(), std::make_pair(std::string("id"), docId));
  if (!result->isSuccess() || !result->hasNext()) return std::nullopt;
  const auto row = result->getNext();
  DocumentInfo info;
  info.docId = docId;
  info.title = row->getValue(0)->getValue<std::string>();
  info.url = row->getValue(1)->getValue<std::string>();
  info.collection = row->getValue(2)->getValue<std::string>();
  info.revision = std::to_string(row->getValue(3)->getValue<int64_t>());
  info.docType = row->getValue(4)->getValue<std::string>();
  info.primary = row->getValue(5)->getValue<std::string>();
  return info;
}

bool IndexStore::upsertEntities(const std::vector<Entity> &entities, std::string *error) {
  if (!ok()) return false;
  auto &conn = mImpl->conn;
  if (!exec(conn, "BEGIN TRANSACTION", error)) return false;
  auto lookup = conn.prepare("MATCH (e:Entity {name:$name}) RETURN e.docs, e.primary_docs");
  auto upsert = conn.prepare(
      "MERGE (e:Entity {name:$name}) SET e.kind=$kind, e.docs=$docs, e.primary_docs=$primaryDocs");
  for (const auto &entity : entities) {
    std::vector<std::string> docs;
    std::vector<std::string> primaryDocs;
    auto existing = conn.execute(lookup.get(), std::make_pair(std::string("name"), entity.name));
    if (existing->isSuccess() && existing->hasNext()) {
      const auto row = existing->getNext();
      const auto split = [](const std::string &joined, std::vector<std::string> &out) {
        size_t start = 0;
        while (start < joined.size()) {
          const auto comma = joined.find(',', start);
          const auto id = joined.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          if (!id.empty()) out.push_back(id);
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
      };
      split(row->getValue(0)->getValue<std::string>(), docs);
      split(row->getValue(1)->getValue<std::string>(), primaryDocs);
    }
    for (const auto &id : entity.docIds)
      if (std::find(docs.begin(), docs.end(), id) == docs.end()) docs.push_back(id);
    for (const auto &id : entity.primaryDocs)
      if (std::find(primaryDocs.begin(), primaryDocs.end(), id) == primaryDocs.end())
        primaryDocs.push_back(id);
    const auto join = [](const std::vector<std::string> &ids) {
      std::string joined;
      for (size_t i = 0; i < ids.size(); ++i) {
        if (i) joined += ',';
        joined += ids[i];
      }
      return joined;
    };
    auto result = conn.execute(upsert.get(), std::make_pair(std::string("name"), entity.name),
                               std::make_pair(std::string("kind"), entity.kind),
                               std::make_pair(std::string("docs"), join(docs)),
                               std::make_pair(std::string("primaryDocs"), join(primaryDocs)));
    if (!result->isSuccess()) {
      if (error) *error = result->getErrorMessage();
      std::string ignored;
      exec(conn, "ROLLBACK", &ignored);
      return false;
    }
  }
  return exec(conn, "COMMIT", error);
}

std::vector<Entity> IndexStore::allEntities() {
  if (!ok()) return {};
  auto result = mImpl->conn.query("MATCH (e:Entity) RETURN e.name, e.kind, e.docs, e.primary_docs");
  if (!result->isSuccess()) return {};
  std::vector<Entity> out;
  while (result->hasNext()) {
    const auto row = result->getNext();
    Entity entity;
    entity.name = row->getValue(0)->getValue<std::string>();
    entity.kind = row->getValue(1)->getValue<std::string>();
    const auto split = [](const std::string &joined, std::vector<std::string> &outIds) {
      size_t start = 0;
      while (start < joined.size()) {
        const auto comma = joined.find(',', start);
        const auto id = joined.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!id.empty()) outIds.push_back(id);
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    };
    split(row->getValue(2)->getValue<std::string>(), entity.docIds);
    split(row->getValue(3)->getValue<std::string>(), entity.primaryDocs);
    out.push_back(std::move(entity));
  }
  return out;
}

bool IndexStore::upsertFaq(const std::vector<FaqEntry> &entries, std::string *error) {
  if (!ok()) return false;
  auto &conn = mImpl->conn;
  if (!exec(conn, "BEGIN TRANSACTION", error)) return false;
  auto insert = conn.prepare(
      "CREATE (f:Faq {docId:$docId, question:$question, answer:$answer, url:$url, embedding:$emb})");
  for (const auto &entry : entries) {
    if (entry.embedding.size() != static_cast<size_t>(kEmbeddingDim)) {
      if (error) *error = "faq embedding dimension mismatch";
      std::string ignored;
      exec(conn, "ROLLBACK", &ignored);
      return false;
    }
    auto result = conn.execute(insert.get(), std::make_pair(std::string("docId"), entry.docId),
                               std::make_pair(std::string("question"), entry.question),
                               std::make_pair(std::string("answer"), entry.answer),
                               std::make_pair(std::string("url"), entry.url),
                               std::make_pair(std::string("emb"), floatArray(entry.embedding)));
    if (!result->isSuccess()) {
      if (error) *error = result->getErrorMessage();
      std::string ignored;
      exec(conn, "ROLLBACK", &ignored);
      return false;
    }
  }
  return exec(conn, "COMMIT", error);
}

std::vector<FaqHit> IndexStore::searchFaq(const std::vector<float> &embedding, int k, std::string *error) {
  if (!ok()) return {};
  if (embedding.size() != static_cast<size_t>(kEmbeddingDim)) {
    if (error) *error = "embedding dimension mismatch";
    return {};
  }
  auto prepared = mImpl->conn.prepare(
      "CALL QUERY_VECTOR_INDEX('Faq','faq_vec',$q,$k) "
      "RETURN node.question, node.answer, node.url, node.docId, distance ORDER BY distance");
  auto result = mImpl->conn.execute(prepared.get(), std::make_pair(std::string("q"), floatArray(embedding)),
                                    std::make_pair(std::string("k"), int64_t(k)));
  if (!result->isSuccess()) {
    if (error) *error = result->getErrorMessage();
    return {};
  }
  std::vector<FaqHit> hits;
  while (result->hasNext()) {
    const auto row = result->getNext();
    hits.push_back({row->getValue(0)->getValue<std::string>(), row->getValue(1)->getValue<std::string>(),
                    row->getValue(2)->getValue<std::string>(), row->getValue(3)->getValue<std::string>(),
                    row->getValue(4)->getValue<double>()});
  }
  return hits;
}

bool IndexStore::upsertPassports(const std::vector<Passport> &passports, std::string *error) {
  if (!ok()) return false;
  auto &conn = mImpl->conn;
  if (!exec(conn, "BEGIN TRANSACTION", error)) return false;
  const auto rollback = [&] {
    std::string ignored;
    exec(conn, "ROLLBACK", &ignored);
  };
  auto merge = conn.prepare(
      "MERGE (p:Passport {name:$name}) SET p.industry=$industry, p.client_type=$clientType, "
      "p.what_built=$whatBuilt, p.deployment=$deployment, p.description=$description, "
      "p.docId=$docId, p.url=$url, p.aliases=$aliases, p.models=$models, p.embedding=$emb");
  const auto join = [](const std::vector<std::string> &values) {
    std::string joined;
    for (const auto &value : values) {
      if (!joined.empty()) joined += '\n';
      joined += value;
    }
    return joined;
  };
  for (const auto &passport : passports) {
    if (passport.embedding.size() != static_cast<size_t>(kEmbeddingDim)) {
      if (error) *error = "passport embedding dimension mismatch";
      rollback();
      return false;
    }
    auto result = conn.execute(merge.get(), std::make_pair(std::string("name"), passport.name),
                               std::make_pair(std::string("industry"), passport.industry),
                               std::make_pair(std::string("clientType"), passport.clientType),
                               std::make_pair(std::string("whatBuilt"), passport.whatBuilt),
                               std::make_pair(std::string("deployment"), passport.deployment),
                               std::make_pair(std::string("description"), passport.description),
                               std::make_pair(std::string("docId"), passport.docId),
                               std::make_pair(std::string("url"), passport.url),
                               std::make_pair(std::string("aliases"), join(passport.aliases)),
                               std::make_pair(std::string("models"), join(passport.models)),
                               std::make_pair(std::string("emb"), floatArray(passport.embedding)));
    if (!result->isSuccess()) {
      if (error) *error = result->getErrorMessage();
      rollback();
      return false;
    }
  }
  return exec(conn, "COMMIT", error);
}

std::vector<Passport> IndexStore::allPassports() {
  if (!ok()) return {};
  auto result = mImpl->conn.query(
      "MATCH (p:Passport) RETURN p.name, p.industry, p.client_type, p.what_built, p.deployment, "
      "p.description, p.docId, p.url, p.aliases, p.models, p.embedding");
  if (!result->isSuccess()) return {};
  const auto split = [](const std::string &joined) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < joined.size()) {
      const auto nl = joined.find('\n', start);
      const auto item = joined.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
      if (!item.empty()) out.push_back(item);
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    return out;
  };
  std::vector<Passport> out;
  while (result->hasNext()) {
    const auto row = result->getNext();
    Passport passport;
    passport.name = row->getValue(0)->getValue<std::string>();
    passport.industry = row->getValue(1)->getValue<std::string>();
    passport.clientType = row->getValue(2)->getValue<std::string>();
    passport.whatBuilt = row->getValue(3)->getValue<std::string>();
    passport.deployment = row->getValue(4)->getValue<std::string>();
    passport.description = row->getValue(5)->getValue<std::string>();
    passport.docId = row->getValue(6)->getValue<std::string>();
    passport.url = row->getValue(7)->getValue<std::string>();
    passport.aliases = split(row->getValue(8)->getValue<std::string>());
    passport.models = split(row->getValue(9)->getValue<std::string>());
    const auto *emb = row->getValue(10);
    for (uint32_t i = 0; i < lbug::common::NestedVal::getChildrenSize(emb); ++i)
      passport.embedding.push_back(lbug::common::NestedVal::getChildVal(emb, i)->getValue<float>());
    out.push_back(std::move(passport));
  }
  return out;
}

std::vector<SearchHit> IndexStore::searchFts(const std::string &query, int k, std::string *error) {
  if (!ok()) return {};
  // QUERY_FTS_INDEX binds only literal arguments; escape the user text.
  std::string escaped = sanitizeUtf8(query);
  for (size_t pos = 0; (pos = escaped.find('\'', pos)) != std::string::npos; pos += 2)
    escaped.replace(pos, 1, "''");
  const std::string sql = "CALL QUERY_FTS_INDEX('Chunk','chunk_fts','" + escaped +
                          "') RETURN node.id, node.docId, node.text, score ORDER BY score DESC LIMIT " +
                          std::to_string(std::max(1, k));
  try {
    auto result = mImpl->conn.query(sql);
    if (!result->isSuccess()) {
      if (error) *error = result->getErrorMessage();
      return {};
    }
    std::vector<SearchHit> hits;
    while (result->hasNext()) {
      const auto row = result->getNext();
      hits.push_back({row->getValue(0)->getValue<int64_t>(), row->getValue(1)->getValue<std::string>(),
                      row->getValue(2)->getValue<std::string>(), row->getValue(3)->getValue<double>()});
    }
    return hits;
  } catch (const std::exception &e) {
    if (error) *error = e.what();
    return {};
  }
}

std::vector<SearchHit> IndexStore::searchVector(const std::vector<float> &embedding, int k, std::string *error) {
  if (!ok()) return {};
  if (embedding.size() != static_cast<size_t>(kEmbeddingDim)) {
    if (error) *error = "embedding dimension mismatch";
    return {};
  }
  auto prepared = mImpl->conn.prepare(
      "CALL QUERY_VECTOR_INDEX('Chunk','chunk_vec',$q,$k) RETURN node.id, node.docId, node.text, distance ORDER BY distance");
  auto result = mImpl->conn.execute(prepared.get(), std::make_pair(std::string("q"), floatArray(embedding)),
                                    std::make_pair(std::string("k"), int64_t(k)));
  if (!result->isSuccess()) {
    if (error) *error = result->getErrorMessage();
    return {};
  }
  std::vector<SearchHit> hits;
  while (result->hasNext()) {
    const auto row = result->getNext();
    hits.push_back({row->getValue(0)->getValue<int64_t>(), row->getValue(1)->getValue<std::string>(),
                    row->getValue(2)->getValue<std::string>(), row->getValue(3)->getValue<double>()});
  }
  return hits;
}

} // namespace sessio::rag
