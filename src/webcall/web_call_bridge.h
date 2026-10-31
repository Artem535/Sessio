#pragma once

#include "participant_model.h"

#include <QObject>
#include <QString>

namespace pcm::webcall {

// The QWebChannel object ("bridge") the call page talks to. It only turns the
// page's events into Qt signals and ParticipantModel upserts, so it has no
// WebEngine dependency and can be unit-tested directly.
class WebCallBridge final : public QObject {
  Q_OBJECT
public:
  explicit WebCallBridge(pcm::video::ParticipantModel *participants, QObject *parent = nullptr);

public slots:
  void joined(const QString &localId);
  void joinFailed(const QString &reason);
  void left();
  void connectionLost(const QString &reason);
  void participantJoined(const QString &id, const QString &name, bool isLocal);
  void participantLeft(const QString &id);
  void mediaError(const QString &reason);
  void audioBridgeLost();

signals:
  void joinedCall(const QString &localId);
  void joinFailedWith(const QString &reason);
  void leftCall();
  void connectionLostWith(const QString &reason);
  void participantAdded(const QString &id);
  void participantRemoved(const QString &id);
  void mediaErrorWith(const QString &reason);
  void audioBridgeDropped();

private:
  pcm::video::ParticipantModel *participants_;
};

} // namespace pcm::webcall
