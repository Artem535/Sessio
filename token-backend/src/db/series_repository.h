#pragma once
#include "db/meetings_repository.h"
namespace pcm::tokenbackend {
struct SeriesInvitation {
  int64_t id;
  std::string uid;
  AccountId account;
  int64_t generation;
  std::string passcodeHash;
  int attempts;
  bool revoked;
};
struct InvitationReplay {
  std::string uid;
  bool reissue;
  int64_t generation;
  std::string encryptedPayload;
  int64_t expiresAt;
};
struct OccurrenceMapping { std::string uid; int64_t originalStartMs; };
// Every method holds the shared connection lock. No method opens a transaction:
// SeriesService owns the transaction spanning generation+replay or room+mapping.
class SeriesRepository {
public:
  explicit SeriesRepository(SqliteConnection &conn) : conn_(conn) {}
  std::optional<SeriesInvitation> byCodeHash(const std::string &hash);
  std::optional<SeriesInvitation> current(const std::string &uid);
  void insert(const std::string &uid, int64_t generation, const std::string &hash, const std::string &passcodeHash);
  void revoke(const std::string &uid, int64_t now);
  void fail(int64_t id, int64_t now);
  std::optional<InvitationReplay> replay(AccountId account, const std::string &key);
  void saveReplay(AccountId account, const std::string &key, const InvitationReplay &replay);
  void retireReplays(int64_t now);
  std::optional<int64_t> meetingId(const std::string &uid, int64_t originalStartMs);
  std::optional<OccurrenceMapping> mapping(int64_t meetingId);
  void map(const std::string &uid, int64_t originalStartMs, int64_t meetingId);
private:
  SqliteConnection &conn_;
};
}
