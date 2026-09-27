#include "crypto/hashing.h"

#include <sodium.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pcm::tokenbackend {

std::string fastHash(const std::string &plaintext) {
  unsigned char out[crypto_generichash_BYTES];
  crypto_generichash(out, sizeof(out),
                      reinterpret_cast<const unsigned char *>(plaintext.data()),
                      plaintext.size(), nullptr, 0);

  std::ostringstream oss;
  for (unsigned char byte : out) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
  }
  return oss.str();
}

bool fastHashMatches(const std::string &plaintext, const std::string &storedHashHex) {
  auto computed = fastHash(plaintext);
  if (computed.size() != storedHashHex.size()) {
    return false;
  }
  return sodium_memcmp(computed.data(), storedHashHex.data(), computed.size()) == 0;
}

// INTERACTIVE (64 MiB / 2 ops) rather than MODERATE (256 MiB) deliberately:
// POST /v1/invitations/{code}/client-token is unauthenticated by design
// (ADR-12) and runs crypto_pwhash_str_verify on every request, so the memory
// limit is an attacker-controlled allocation on a shared VPS. At MODERATE,
// ten concurrent requests reserve ~2.5 GiB and can take the box (and the
// LiveKit server next to it) down; at INTERACTIVE the same burst costs
// ~640 MiB. The brute-force budget this gives up is compensated by ADR-12's
// hard 5-attempt-per-invitation cap, which is the real security boundary for
// a 6-digit passcode — not the KDF cost.
std::string hashPasscode(const std::string &passcode) {
  char out[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(out, passcode.c_str(), passcode.size(),
                         crypto_pwhash_OPSLIMIT_INTERACTIVE,
                         crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
    throw std::runtime_error("passcode hashing failed (out of memory)");
  }
  return std::string(out);
}

bool passcodeMatches(const std::string &passcode, const std::string &storedHash) {
  return crypto_pwhash_str_verify(storedHash.c_str(), passcode.c_str(), passcode.size()) == 0;
}

} // namespace pcm::tokenbackend
