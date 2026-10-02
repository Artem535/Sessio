#pragma once

#include "meeting_descriptor.h"

#include <QObject>
#include <QString>

namespace pcm::meeting {

struct MeetingCreateRequest {
  QString rawMeetingUrl;
  QString scheduledStartIso; // ISO-8601, only used by the LiveKit provider
  QString scheduledEndIso;   // ISO-8601, only used by the LiveKit provider
};

class MeetingProvider : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~MeetingProvider() override = default;

  virtual void create(const MeetingCreateRequest &request) = 0;
  virtual void cancel(const QString &meetingRef) = 0;

  // Live settings propagation: called by MeetingCoordinator when the
  // specialist changes the token-backend URL or the keychain bearer
  // credential changes, without requiring an app restart. Default no-ops so
  // a provider with nothing to propagate (ExternalUrlMeetingProvider) needs
  // no changes at all; LiveKitMeetingProvider overrides both.
  virtual void setTokenBackendBaseUrl(const QString &baseUrl) { Q_UNUSED(baseUrl); }
  virtual void setBearerCredential(const QString &credential) { Q_UNUSED(credential); }

signals:
  void created(pcm::meeting::MeetingDescriptor descriptor);
  void createFailed(QString error);
  void canceled();
  void cancelFailed(QString error);
};

} // namespace pcm::meeting
