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
  explicit ParticipantTile(const Participant &participant, QWidget *parent = nullptr);
  void updateParticipant(const Participant &participant);
  void attachSource(VideoFrameSource *source);
  [[nodiscard]] bool isLocal() const { return mParticipant.isLocal; }
  [[nodiscard]] QString identity() const { return mParticipant.id; }
  // Rounds the tile's corners (clipping the video and placeholder inside it); 0 = square.
  void setCornerRadius(int radius);
  [[nodiscard]] int cornerRadius() const { return mCornerRadius; }

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void refreshMedia();
  Participant mParticipant;
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
