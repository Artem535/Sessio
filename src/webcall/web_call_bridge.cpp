#include "web_call_bridge.h"

namespace pcm::webcall {

WebCallBridge::WebCallBridge(pcm::video::ParticipantModel *participants, QObject *parent)
    : QObject(parent), participants_(participants) {}

void WebCallBridge::joined(const QString &localId) { emit joinedCall(localId); }
void WebCallBridge::joinFailed(const QString &reason) { emit joinFailedWith(reason); }
void WebCallBridge::left() { emit leftCall(); }
void WebCallBridge::connectionLost(const QString &reason) { emit connectionLostWith(reason); }
void WebCallBridge::mediaError(const QString &reason) { emit mediaErrorWith(reason); }
void WebCallBridge::audioBridgeLost() { emit audioBridgeDropped(); }

void WebCallBridge::participantJoined(const QString &id, const QString &name, bool isLocal) {
  // The page may announce a participant again (e.g. after a reconnect of the
  // conference); the model and VideoSession must see it once.
  if (participants_->participant(id)) return;
  pcm::video::Participant p;
  p.id = id;
  p.displayName = name;
  p.isLocal = isLocal;
  participants_->upsert(p);
  emit participantAdded(id);
}

void WebCallBridge::participantLeft(const QString &id) {
  if (!participants_->participant(id)) return;
  participants_->remove(id);
  emit participantRemoved(id);
}

} // namespace pcm::webcall
