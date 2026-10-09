#pragma once

#include "participant_model.h"
#include "video_frame_source.h"
#include <QPointer>
#include <QWidget>

class QLabel;
namespace pcm::video {
class RemoteVideoRenderer;

class ParticipantTile final : public QWidget {
  Q_OBJECT
public:
  // A participant has a camera tile and, while sharing, a separate screen tile.
  enum class Kind { Camera, Screen };
  explicit ParticipantTile(const Participant &participant, QWidget *parent = nullptr,
                           Kind kind = Kind::Camera);
  void updateParticipant(const Participant &participant);
  void attachSource(VideoFrameSource *source);
  [[nodiscard]] bool isLocal() const { return mKind == Kind::Camera && mParticipant.isLocal; }
  [[nodiscard]] bool isScreen() const { return mKind == Kind::Screen; }
  [[nodiscard]] QString identity() const { return mParticipant.id; }
  // Unique among tiles: a participant's screen has its own tile next to the camera one.
  [[nodiscard]] QString key() const { return mKind == Kind::Screen ? mParticipant.id + QStringLiteral("#screen") : mParticipant.id; }
  // Rounds the tile's corners (clipping the video and placeholder inside it); 0 = square.
  void setCornerRadius(int radius);
  [[nodiscard]] int cornerRadius() const { return mCornerRadius; }

signals:
  // A press-and-release on the tile; used to bring a screen onto the stage.
  void clicked();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void refreshMedia();
  Participant mParticipant;
  Kind mKind;
  QPointer<VideoFrameSource> mSource;
  QMetaObject::Connection mFrameConnection;
  QMetaObject::Connection mDestroyedConnection;
  void applyCornerMask();
  int mCornerRadius{0};
  RemoteVideoRenderer *mRenderer;
  QLabel *mPlaceholder;
  QLabel *mName;
};
} // namespace pcm::video
