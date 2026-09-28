#pragma once

#include "meeting_provider.h"
#include "token_backend_client.h"

#include <memory>

namespace pcm::meeting {

// Backs a LiveKit meeting through the token-backend's HTTP endpoints:
// POST /v1/meetings to create, POST /v1/meetings/{ref}/invalidate to cancel.
class LiveKitMeetingProvider final : public MeetingProvider {
  Q_OBJECT

public:
  LiveKitMeetingProvider(QString tokenBackendBaseUrl, QString bearerCredential,
                         QObject *parent = nullptr);

  void create(const MeetingCreateRequest &request) override;
  void cancel(const QString &meetingRef) override;

  // Live settings propagation (see MeetingProvider's doc comment). Both
  // read fresh by create()/cancel() on every call — mClient/mBearerCredential
  // are plain state, never cached elsewhere.
  void setTokenBackendBaseUrl(const QString &baseUrl) override;
  void setBearerCredential(const QString &credential) override;

private:
  std::unique_ptr<pcm::tokenclient::TokenBackendClient> mClient;
  QString mBearerCredential;
};

} // namespace pcm::meeting
