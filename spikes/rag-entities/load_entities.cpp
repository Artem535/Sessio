// Load entities.json (NER extraction output) into a rag-core index.
// Usage: load_entities <index.lbug dir> <extension dir> <entities.json>
#include "index_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 4) return 2;
  QFile file(QString::fromUtf8(argv[3]));
  if (!file.open(QIODevice::ReadOnly)) {
    std::fprintf(stderr, "cannot open %s\n", argv[3]);
    return 1;
  }
  const auto array = QJsonDocument::fromJson(file.readAll()).array();
  std::vector<sessio::rag::Entity> entities;
  for (const auto &value : array) {
    const auto object = value.toObject();
    sessio::rag::Entity entity;
    entity.name = object["name"].toString().toStdString();
    entity.kind = object["kind"].toString().toStdString();
    for (const auto &doc : object["docs"].toArray()) entity.docIds.push_back(doc.toString().toStdString());
    if (!entity.name.empty()) entities.push_back(std::move(entity));
  }
  std::string error;
  sessio::rag::IndexStore store(argv[1], argv[2], &error);
  if (!store.ok()) {
    std::fprintf(stderr, "index: %s\n", error.c_str());
    return 1;
  }
  if (!store.upsertEntities(entities, &error)) {
    std::fprintf(stderr, "upsert: %s\n", error.c_str());
    return 1;
  }
  std::printf("loaded %zu entities, total %zu\n", entities.size(), store.allEntities().size());
  return 0;
}
