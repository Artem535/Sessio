#include "livekit_video_frame_source.h"

#include <livekit/livekit.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

namespace {
bool isPattern(const QImage &frame, bool red) {
  if (frame.isNull()) return false;
  const QColor pixel = frame.pixelColor(0, 0);
  // SDK native tracks round-trip through I420; allow small color-conversion
  // differences while still distinguishing the two independent streams.
  return red ? pixel.red() > 240 && pixel.green() < 16 && pixel.blue() < 16
             : pixel.blue() > 240 && pixel.green() < 16 && pixel.red() < 16;
}

livekit::VideoFrame solidFrame(bool red) {
  auto frame = livekit::VideoFrame::create(32, 18, livekit::VideoBufferType::RGBA);
  for (int pixel = 0; pixel < 32 * 18; ++pixel) {
    frame.data()[pixel * 4] = red ? 255 : 0;
    frame.data()[pixel * 4 + 1] = 0;
    frame.data()[pixel * 4 + 2] = red ? 0 : 255;
    frame.data()[pixel * 4 + 3] = 255;
  }
  return frame;
}

bool framesArrive(pcm::video::LiveKitVideoFrameSource &a,
                  pcm::video::LiveKitVideoFrameSource &b,
                  livekit::VideoSource &red, livekit::VideoSource &blue,
                  bool bothBlue = false) {
  auto redFrame = solidFrame(true);
  auto blueFrame = solidFrame(false);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    red.captureFrame(redFrame);
    blue.captureFrame(blueFrame);
    QCoreApplication::processEvents();
    const auto imageA = a.latestFrame();
    const auto imageB = b.latestFrame();
    if (isPattern(imageA, !bothBlue) && isPattern(imageB, false)) return true;
    QThread::msleep(5);
  }
  return false;
}

bool run() {
  auto red = std::make_shared<livekit::VideoSource>(32, 18);
  auto blue = std::make_shared<livekit::VideoSource>(32, 18);
  auto trackA = livekit::LocalVideoTrack::createLocalVideoTrack("synthetic-red", red);
  auto trackB = livekit::LocalVideoTrack::createLocalVideoTrack("synthetic-blue", blue);
  pcm::video::LiveKitVideoFrameSource a, b;
  a.attachTrack(trackA);
  b.attachTrack(trackB);
  if (!framesArrive(a, b, *red, *blue)) {
    std::cerr << "independent SDK video streams did not deliver patterns" << std::endl;
    for (const auto *source : {&a, &b}) {
      const auto frame = source->latestFrame();
      std::cerr << "frame " << frame.width() << "x" << frame.height();
      if (!frame.isNull()) std::cerr << " rgb " << frame.pixelColor(0, 0).name().toStdString();
      std::cerr << std::endl;
    }
    return false;
  }
  a.attachTrack(trackB);
  if (!a.latestFrame().isNull() || !framesArrive(a, b, *red, *blue, true)) {
    std::cerr << "track replacement did not clear/deliver new pattern" << std::endl;
    return false;
  }
  a.detach();
  b.detach();
  if (!a.latestFrame().isNull() || !b.latestFrame().isNull()) return false;
  QElapsedTimer closeTimer;
  closeTimer.start();
  for (int cycle = 0; cycle < 20; ++cycle) {
    pcm::video::LiveKitVideoFrameSource blocked;
    blocked.attachTrack(trackA); // no capture: reader blocks in read()
    QThread::msleep(2);
    blocked.detach();
  }
  if (closeTimer.elapsed() > 3000) {
    std::cerr << "blocked reader close exceeded deadline" << std::endl;
    return false;
  }
  return true; // all sources/tracks die before SDK shutdown below
}
} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  livekit::initialize(livekit::LogLevel::Warn);
  const bool ok = run();
  livekit::shutdown();
  if (!ok) return 1;
  std::cout << "independent SDK local frames, replacement and 20 blocked-reader closes passed"
            << std::endl;
}
