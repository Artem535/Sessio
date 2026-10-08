#include "participant_tile.h"
#include "remote_video_renderer.h"

#include <QLabel>
#include <QRegion>
#include <QResizeEvent>
#include <QTextDocument>
#include <algorithm>

namespace pcm::video {

ParticipantTile::ParticipantTile(const Participant &participant, QWidget *parent, Kind kind)
    : QWidget(parent), mKind(kind), mRenderer(new RemoteVideoRenderer(this)),
      mPlaceholder(new QLabel(this)), mName(new QLabel(this)) {
  setObjectName((kind == Kind::Screen ? QStringLiteral("participantScreenTile_")
                                      : QStringLiteral("participantTile_")) + participant.id);
  setMinimumSize(0, 0);
  mRenderer->setObjectName("participantRenderer");
  mPlaceholder->setObjectName("cameraOffPlaceholder");
  mPlaceholder->setTextFormat(Qt::PlainText);
  mPlaceholder->setAlignment(Qt::AlignCenter);
  mPlaceholder->setStyleSheet("background: #20242c; color: #c8d0dc;");
  mName->setObjectName("participantName");
  mName->setTextFormat(Qt::PlainText);
  mName->setStyleSheet("background: #141414; color: white; padding: 4px 8px; border-radius: 6px;");
  updateParticipant(participant);
}

void ParticipantTile::updateParticipant(const Participant &participant) {
  mParticipant = participant;
  const QString name = participant.displayName.isEmpty() ? tr("Participant") : participant.displayName;
  if (mKind == Kind::Screen)
    mName->setText(participant.isLocal ? tr("Your screen") : tr("%1's screen").arg(name));
  else
    mName->setText(participant.isLocal ? tr("%1 (You)").arg(name) : name);
  mName->setGeometry(8, 8, std::min({std::max(0, width() - 72), mName->sizeHint().width(), 320}), std::min(30, height()));
  mName->setToolTip(Qt::convertFromPlainText(mName->text()));
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
  const bool active = mKind == Kind::Screen ? mParticipant.screenSharing : mParticipant.cameraEnabled;
  const bool hasFrame = active && mSource && !mSource->latestFrame().isNull();
  // A renderer remains hidden until a usable frame arrives: offscreen Qt can
  // exercise placeholders and layout without constructing an OpenGL context.
  mRenderer->setVisible(hasFrame);
  mPlaceholder->setVisible(!hasFrame);
  mPlaceholder->setText(active ? (mKind == Kind::Screen ? tr("Waiting for screen...") : tr("Waiting for video..."))
                               : (mKind == Kind::Screen ? tr("Screen sharing stopped") : tr("Camera off")));
  mName->raise();
}

void ParticipantTile::setCornerRadius(int radius) {
  radius = std::max(0, radius);
  if (radius == mCornerRadius) {
    return;
  }
  mCornerRadius = radius;
  applyCornerMask();
}

// A rectangle with its four corners cut by quarter circles. Built from regions (a cross plus four
// ellipses) so it clips the child OpenGL video widget as well as the placeholder.
void ParticipantTile::applyCornerMask() {
  if (mCornerRadius <= 0 || width() <= 0 || height() <= 0) {
    clearMask();
    return;
  }
  const int r = std::min({mCornerRadius, width() / 2, height() / 2});
  const int d = 2 * r;
  QRegion region(QRect(r, 0, width() - d, height()));
  region += QRegion(QRect(0, r, width(), height() - d));
  region += QRegion(QRect(0, 0, d, d), QRegion::Ellipse);
  region += QRegion(QRect(width() - d, 0, d, d), QRegion::Ellipse);
  region += QRegion(QRect(0, height() - d, d, d), QRegion::Ellipse);
  region += QRegion(QRect(width() - d, height() - d, d, d), QRegion::Ellipse);
  setMask(region);
}

void ParticipantTile::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  applyCornerMask();
  mRenderer->setGeometry(rect());
  mPlaceholder->setGeometry(rect());
  mName->setGeometry(8, 8, std::min({std::max(0, width() - 72), mName->sizeHint().width(), 320}), std::min(30, height()));
  mName->raise();
}

} // namespace pcm::video
