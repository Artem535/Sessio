#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>

namespace pcm::video {

// Latest-frame mailbox. Stop producers before destruction; pending owner-thread
// notifications are canceled by QObject. Images never borrow SDK storage.
class VideoFrameSource : public QObject {
  Q_OBJECT
public:
  explicit VideoFrameSource(QObject *parent = nullptr);
  void submitFrame(const QImage &frame);
  void clear();
  [[nodiscard]] QImage latestFrame() const;

signals:
  void frameAvailable();

private:
  void replaceFrame(QImage frame);
  mutable QMutex mMutex;
  QImage mLatestFrame;
  bool mNotificationPending{false};
};

} // namespace pcm::video
