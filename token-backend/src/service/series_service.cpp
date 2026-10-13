#include "service/series_service.h"
#include "service/display_name.h"
#include "service/schedule_wire.h"
#include "db/series_repository.h"
#include "crypto/hashing.h"
#include "crypto/random_token.h"
#include "controller/invitation_url.h"
#include <sodium.h>
#include <array>
#include <chrono>
#include <stdexcept>
#include <deque>
#include <map>

namespace pcm::tokenbackend {
namespace {
int64_t systemNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
template<class T> Result<T> error(ServiceError e) { return {{}, e}; }
std::string replayScope(AccountId account, const std::string &key, const InvitationReplay &r) {
  return "pcm-series-replay-v1:" + std::to_string(account) + ":" + key + ":" + r.uid + ":" +
      std::to_string(r.reissue) + ":" + std::to_string(r.generation) + ":" + std::to_string(r.expiresAt);
}
}
struct SeriesService::Impl {
  SqliteConnection &conn;
  Authorizer &authorizer;
  MeetingsRepository &meetings;
  const Config &config;
  std::string endpoint;
  ServiceClock clock;
  ScheduleRepository schedules;
  SeriesRepository repository;
  std::map<std::string, std::deque<int64_t>> limits;
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> replayKey{};

  Impl(SqliteConnection &c, Authorizer &a, MeetingsRepository &m, const Config &cfg,
       std::string url, ServiceClock time)
      : conn(c), authorizer(a), meetings(m), config(cfg), endpoint(std::move(url)),
        clock(time ? std::move(time) : ServiceClock(systemNow)), schedules(c), repository(c) {
    if (cfg.liveKitApiSecret.empty()) throw std::invalid_argument("LIVEKIT_API_SECRET is required");
    // Domain separation from JWT signing, stable over process restarts.
    constexpr std::string_view domain = "sessio:series-invitation-replay:v1";
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, reinterpret_cast<const unsigned char *>(cfg.liveKitApiSecret.data()), cfg.liveKitApiSecret.size());
    crypto_auth_hmacsha256_update(&state, reinterpret_cast<const unsigned char *>(domain.data()), domain.size());
    crypto_auth_hmacsha256_final(&state, replayKey.data());
    repository.retireReplays(clock());
  }
  ~Impl() { sodium_memzero(replayKey.data(), replayKey.size()); }
  bool allowed(const std::string &key, size_t budget) {
    // Protected by the same connection lock as all service entry points.
    const auto now = clock();
    for (auto it = limits.begin(); it != limits.end();) {
      auto &events = it->second;
      while (!events.empty() && events.front() <= now - 60) events.pop_front();
      if (events.empty()) it = limits.erase(it); else ++it;
    }
    auto &events = limits[key];
    if (events.size() >= budget) return false;
    events.push_back(now); return true;
  }
  std::string encrypt(const std::string &plain, const std::string &scope) {
    std::string bytes(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    auto *data = reinterpret_cast<unsigned char *>(bytes.data());
    randombytes_buf(data, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    crypto_aead_xchacha20poly1305_ietf_encrypt(data + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, nullptr,
        reinterpret_cast<const unsigned char *>(plain.data()), plain.size(),
        reinterpret_cast<const unsigned char *>(scope.data()), scope.size(), nullptr, data, replayKey.data());
    std::string hex(bytes.size() * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), data, bytes.size()); hex.pop_back(); return hex;
  }
  std::optional<std::string> decrypt(const std::string &hex, const std::string &scope) {
    std::string bytes(hex.size() / 2, '\0'); size_t size = 0;
    auto *data = reinterpret_cast<unsigned char *>(bytes.data());
    if (sodium_hex2bin(data, bytes.size(), hex.data(), hex.size(), nullptr, &size, nullptr) != 0 ||
        size < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES) return {};
    std::string plain(size, '\0'); unsigned long long length = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char *>(plain.data()), &length, nullptr,
        data + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, size - crypto_aead_xchacha20poly1305_ietf_NPUBBYTES,
        reinterpret_cast<const unsigned char *>(scope.data()), scope.size(), data, replayKey.data()) != 0) return {};
    plain.resize(length); return plain;
  }
  Result<TokenResult> token(AccountId account, const std::string &uid, std::optional<int64_t> original,
                            bool client, const std::string &name) {
    const auto normalised = normaliseDisplayName(name, false);
    if (!normalised) return error<TokenResult>(ServiceError::InvalidDisplayName);
    // Caller keeps the shared lock until JWT mint; snapshot writers cannot interleave.
    auto stored = schedules.get(account, uid);
    if (!stored) return error<TokenResult>(ServiceError::NotFound);
    const int64_t now = clock();
    auto selected = schedule::resolve(stored->snapshot, now * 1000, 300, 900);
    if (selected.status == schedule::Status::Ambiguous) return error<TokenResult>(ServiceError::AmbiguousOccurrence);
    if (selected.status == schedule::Status::Invalid || selected.status == schedule::Status::BudgetExceeded)
      return error<TokenResult>(ServiceError::ScheduleUnavailable);
    if (!selected.occurrence || (original && *original != selected.occurrence->originalStartMs))
      return error<TokenResult>(ServiceError::OccurrenceUnavailable);
    const auto &o = *selected.occurrence;
    // One transaction and one mapping implementation for both participant paths.
    SqliteTransaction transaction(conn);
    auto id = repository.meetingId(uid, o.originalStartMs);
    auto meeting = id ? meetings.findById(*id) : std::optional<Meeting>{};
    if (!id) {
      meeting = meetings.create(account, utcTimestamp(o.startMs), utcTimestamp(o.endMs));
      repository.map(uid, o.originalStartMs, meeting->id);
    }
    if (!meeting || meeting->status != "active") return error<TokenResult>(ServiceError::OccurrenceUnavailable);
    VideoGrants grants; grants.room = meeting->roomName;
    std::string identity = (client ? "client-" : "practitioner-") + meeting->meetingRef;
    identity += "-" + generateUrlSafeToken(16);
    auto jwt = mintLiveKitJwt(config.liveKitApiKey, config.liveKitApiSecret, identity, grants,
        config.tokenTtlSeconds, client ? R"({"role":"client"})" : R"({"role":"practitioner"})", *normalised, now);
    transaction.commit();
    return {TokenResult{endpoint, meeting->roomName, jwt, now + config.tokenTtlSeconds}, {}};
  }
};
SeriesService::SeriesService(SqliteConnection &conn, Authorizer &authorizer, MeetingsRepository &meetings,
                             const Config &config, std::string endpoint, ServiceClock clock)
    : impl_(std::make_unique<Impl>(conn, authorizer, meetings, config, std::move(endpoint), std::move(clock))) {}
SeriesService::~SeriesService() = default;

Result<SeriesService::Invitation> SeriesService::invitation(const std::string &credential, const std::string &uid,
                                                           const std::string &key, const std::string &json) {
  auto &p = *impl_; auto lock = p.conn.lock();
  auto account = p.authorizer.authorize(credential);
  if (!account) return error<Invitation>(ServiceError::Unauthorized);
  if (!p.allowed("mutation:" + std::to_string(*account), 10)) return error<Invitation>(ServiceError::TooManyAttempts);
  auto normalizedKey = normalizeSeriesUid(key);
  if (!normalizedKey || json.size() > 4096) return error<Invitation>(ServiceError::InvalidRequest);
  bool reissue;
  try { reissue = parseSeriesInvitationJson(json); }
  catch (const std::invalid_argument &) { return error<Invitation>(ServiceError::InvalidRequest); }
  auto stored = p.schedules.get(*account, uid);
  if (!stored) return error<Invitation>(ServiceError::NotFound);
  const auto now = p.clock();
  p.repository.retireReplays(now);
  SqliteTransaction transaction(p.conn);
  if (auto replay = p.repository.replay(*account, *normalizedKey)) {
    if (replay->uid != stored->seriesUid || replay->reissue != reissue)
      return error<Invitation>(ServiceError::IdempotencyConflict);
    if (replay->expiresAt <= now || replay->encryptedPayload.empty()) return error<Invitation>(ServiceError::ReplayExpired);
    auto plain = p.decrypt(replay->encryptedPayload, replayScope(*account, *normalizedKey, *replay));
    if (!plain) return error<Invitation>(ServiceError::ReplayExpired);
    auto split = plain->find('\n');
    if (split == std::string::npos) return error<Invitation>(ServiceError::ReplayExpired);
    auto urlSplit = plain->find('\n', split + 1);
    if (urlSplit == std::string::npos) return error<Invitation>(ServiceError::ReplayExpired);
    return {Invitation{plain->substr(0, split), plain->substr(split + 1, urlSplit - split - 1),
                       replay->generation, plain->substr(urlSplit + 1)}, {}};
  }
  auto current = p.repository.current(stored->seriesUid);
  if (current && !reissue) return error<Invitation>(ServiceError::InvitationExists);
  Invitation result{generateUrlSafeToken(24), generateNumericPasscode(), current ? current->generation + 1 : 1};
  result.invitationUrl = formatInvitationUrl(p.config.invitationBaseUrl, result.code, result.passcode);
  p.repository.revoke(stored->seriesUid, now);
  p.repository.insert(stored->seriesUid, result.generation, fastHash(result.code), hashPasscode(result.passcode));
  InvitationReplay replay{stored->seriesUid, reissue, result.generation, {}, now + 86400};
  replay.encryptedPayload = p.encrypt(result.code + "\n" + result.passcode + "\n" + result.invitationUrl,
                                     replayScope(*account, *normalizedKey, replay));
  p.repository.saveReplay(*account, *normalizedKey, replay);
  transaction.commit(); return {result, {}};
}
Result<std::monostate> SeriesService::revoke(const std::string &credential, const std::string &uid) {
  auto &p = *impl_; auto lock = p.conn.lock();
  auto account = p.authorizer.authorize(credential);
  if (!account) return error<std::monostate>(ServiceError::Unauthorized);
  if (!p.allowed("mutation:" + std::to_string(*account), 10)) return error<std::monostate>(ServiceError::TooManyAttempts);
  auto stored = p.schedules.get(*account, uid);
  if (!stored) return error<std::monostate>(ServiceError::NotFound);
  p.repository.revoke(stored->seriesUid, p.clock());
  return {std::monostate{}, {}};
}
Result<TokenResult> SeriesService::specialistToken(const std::string &credential, const std::string &uid,
                                                 int64_t original, const std::string &name) {
  auto &p = *impl_; auto lock = p.conn.lock();
  auto account = p.authorizer.authorize(credential);
  if (!account) return error<TokenResult>(ServiceError::Unauthorized);
  if (!p.allowed("token:" + std::to_string(*account), 30)) return error<TokenResult>(ServiceError::TooManyAttempts);
  auto normalized = normalizeSeriesUid(uid);
  if (!normalized) return error<TokenResult>(ServiceError::NotFound);
  return p.token(*account, *normalized, original, false, name);
}
std::optional<Result<TokenResult>> SeriesService::clientToken(const std::string &code, const std::string &passcode,
                                                              const std::string &name) {
  auto &p = *impl_; auto lock = p.conn.lock();
  auto invitation = p.repository.byCodeHash(fastHash(code));
  if (!invitation) return {};
  if (!p.allowed("invitation:" + std::to_string(invitation->id), 30)) return error<TokenResult>(ServiceError::TooManyAttempts);
  if (invitation->revoked) return error<TokenResult>(ServiceError::InvitationRevoked);
  if (passcode.empty() || passcode.size() > 256) return error<TokenResult>(ServiceError::InvalidRequest);
  if (!passcodeMatches(passcode, invitation->passcodeHash)) {
    p.repository.fail(invitation->id, p.clock());
    return error<TokenResult>(invitation->attempts >= 5 ? ServiceError::InvitationRevoked : ServiceError::WrongPasscode);
  }
  return p.token(invitation->account, invitation->uid, {}, true, name);
}
std::optional<Result<TokenResult>> SeriesService::mappedToken(const Meeting &meeting, bool client, const std::string &name) {
  auto &p = *impl_; auto lock = p.conn.lock();
  auto mapping = p.repository.mapping(meeting.id);
  if (!mapping) return {};
  if (!p.allowed("token:" + std::to_string(meeting.accountId), 30)) return error<TokenResult>(ServiceError::TooManyAttempts);
  return p.token(meeting.accountId, mapping->uid, mapping->originalStartMs, client, name);
}
}
