#include "participant_tile.h"
#include "remote_video_renderer.h"

#include <QLabel>
#include <QResizeEvent>
#include <algorithm>

namespace pcm::video {

ParticipantTile::ParticipantTile(const Participant &participant, QWidget *parent)
    : QWidget(parent), mRenderer(new RemoteVideoRenderer(this)),
      mPlaceholder(new QLabel(this)), mName(new QLabel(this)) {
  setObjectName(QStringLiteral("participantTile_") + participant.id);
  setMinimumSize(0, 0);
  mRenderer->setObjectName("participantRenderer");
  mPlaceholder->setObjectName("cameraOffPlaceholder");
  mPlaceholder->setTextFormat(Qt::PlainText);
  mPlaceholder->setAlignment(Qt::AlignCenter);
  mPlaceholder->setStyleSheet("background: #20242c; color: #c8d0dc;");
  mName->setObjectName("participantName");
  mName->setTextFormat(Qt::PlainText);
  mName->setStyleSheet("background: #141414; color: white; padding: 4px 8px;");
  updateParticipant(participant);
}

void ParticipantTile::updateParticipant(const Participant &participant) {
  mParticipant = participant;
  const QString name = participant.displayName.isEmpty() ? tr("Participant") : participant.displayName;
  mName->setText(participant.isLocal ? tr("%1 (You)").arg(name) : name);
  mName->setToolTip(mName->text());
  setAccessibleName(mName->text());
  refreshMedia();
}

void ParticipantTile::attachSource(VideoFrameSource *source) {
  if (mSource == source)
    return;
  disconnect(mFrameConnection);
  disconnect(mDestroyedConnection);
  mSource = source;
  mRenderer->attachSource(source);
  if (source) {
    mFrameConnection = connect(source, &VideoFrameSource::frameAvailable, this, &ParticipantTile::refreshMedia);
    mDestroyedConnection = connect(source, &QObject::destroyed, this, [this] {
      mSource = nullptr;
      refreshMedia();
    });
  }
  refreshMedia();
}

void ParticipantTile::refreshMedia() {
  const bool hasFrame = mParticipant.cameraEnabled && mSource && !mSource->latestFrame().isNull();
  // A renderer remains hidden until a usable frame arrives: offscreen Qt can
  // exercise placeholders and layout without constructing an OpenGL context.
  mRenderer->setVisible(hasFrame);
  mPlaceholder->setVisible(!hasFrame);
  mPlaceholder->setText(mParticipant.cameraEnabled ? tr("Waiting for video...") : tr("Camera off"));
  mName->raise();
}

void ParticipantTile::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  mRenderer->setGeometry(rect());
  mPlaceholder->setGeometry(rect());
  mName->setGeometry(0, std::max(0, height() - 30), width(), std::min(30, height()));
  mName->raise();
}

} // namespace pcm::video
