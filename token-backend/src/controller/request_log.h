#pragma once

#include "service/meeting_service.h"

#include "oatpp/core/base/Environment.hpp"

#include <optional>
#include <string>

namespace pcm::tokenbackend {

// Minimal request/audit logging. The goal is "an operator has something to
// look at after an incident", not observability — one line per request, plus a
// louder line for the security-relevant rejections.
//
// NOTHING SECRET IS EVER LOGGED. Not the bearer credential, not the invitation
// code, not the passcode, not the minted JWT. Note in particular that the
// client-token request path *contains* the invitation code, so these helpers
// take a fixed route template rather than the live path, and a `subject` that
// callers only ever fill with a non-secret identifier (a meetingRef, which is
// useless without the bearer credential). Adding a log line that interpolates
// a raw request path here would leak the invitation code into the log.

constexpr const char *kRequestLogTag = "pcm-token-backend/http";
constexpr const char *kAuditLogTag = "pcm-token-backend/audit";

inline const char *serviceErrorName(ServiceError err) {
  switch (err) {
  case ServiceError::Unauthorized:
    return "unauthorized";
  case ServiceError::NotFound:
    return "not_found";
  case ServiceError::WrongPasscode:
    return "wrong_passcode";
  case ServiceError::TooManyAttempts:
    return "too_many_attempts";
  case ServiceError::MeetingWindowClosed:
    return "meeting_window_closed";
  }
  return "unknown_error";
}

// The rejections an operator would want to reconstruct after an incident:
// someone probing credentials, or grinding passcodes against an invitation.
inline bool isSecurityEvent(ServiceError err) {
  switch (err) {
  case ServiceError::Unauthorized:
  case ServiceError::WrongPasscode:
  case ServiceError::TooManyAttempts:
    return true;
  case ServiceError::NotFound:
  case ServiceError::MeetingWindowClosed:
    return false;
  }
  return false;
}

inline void logRequestOk(const char *method, const char *route, const std::string &subject,
                          int status) {
  OATPP_LOGI(kRequestLogTag, "%s %s subject=%s -> %d ok", method, route,
             subject.empty() ? "-" : subject.c_str(), status);
}

inline void logRequestFailure(const char *method, const char *route, const std::string &subject,
                               int status, const char *outcome) {
  OATPP_LOGI(kRequestLogTag, "%s %s subject=%s -> %d %s", method, route,
             subject.empty() ? "-" : subject.c_str(), status, outcome);
}

// Emitted in addition to the request line, at error level, so the
// security-relevant events stand out in an otherwise chatty log.
inline void logSecurityEvent(const char *method, const char *route, const std::string &subject,
                              ServiceError err) {
  OATPP_LOGE(kAuditLogTag, "rejected %s %s subject=%s reason=%s", method, route,
             subject.empty() ? "-" : subject.c_str(), serviceErrorName(err));
}

// One call covers the whole failure path: the request line plus, when it
// matters, the audit line.
inline void logServiceFailure(const char *method, const char *route, const std::string &subject,
                               int status, ServiceError err) {
  logRequestFailure(method, route, subject, status, serviceErrorName(err));
  if (isSecurityEvent(err)) {
    logSecurityEvent(method, route, subject, err);
  }
}

} // namespace pcm::tokenbackend
