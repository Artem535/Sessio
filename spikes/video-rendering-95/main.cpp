// Throwaway benchmark for Sessio #95. Synthetic decoded RGBA frames only:
// no camera, microphone, credentials, network, codec, or participant data.
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QGridLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sys/resource.h>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

double cpuSeconds() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
         usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}

double readMetric(const QString &path) {
  if (path.isEmpty()) {
    return -1;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return -1;
  }
  bool ok = false;
  const double value = file.readAll().trimmed().toDouble(&ok);
  return ok ? value : -1;
}

double residentMiB() {
  QFile file(QStringLiteral("/proc/self/status"));
  if (!file.open(QIODevice::ReadOnly)) {
    return -1;
  }
  for (const auto &line : file.readAll().split('\n')) {
    if (line.startsWith("VmRSS:")) {
      return line.mid(6).simplified().split(' ').front().toDouble() / 1024;
    }
  }
  return -1;
}

double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) {
    return -1;
  }
  std::sort(values.begin(), values.end());
  return values[static_cast<std::size_t>((values.size() - 1) * fraction)];
}

class Tile final : public QOpenGLWidget {
public:
  Tile(int id, QSize sourceSize, int fps, bool cpuScale, QWidget *parent)
      : QOpenGLWidget(parent), mId(id), mFps(fps), mCpuScale(cpuScale) {
    // Pre-generate 3 distinct frames per source. Frame generation is excluded
    // from steady-state CPU measurements; source pool memory is reported.
    for (int variant = 0; variant < 3; ++variant) {
      QImage frame(sourceSize, QImage::Format_RGBA8888);
      for (int y = 0; y < frame.height(); ++y) {
        auto *pixels = frame.scanLine(y);
        for (int x = 0; x < frame.width(); ++x) {
          pixels[4 * x] = (x + id * 37 + variant * 51) % 256;
          pixels[4 * x + 1] = (y + id * 19 + variant * 29) % 256;
          pixels[4 * x + 2] = (x / 8 + y / 8 + id * 53 + variant * 71) % 256;
          pixels[4 * x + 3] = 255;
        }
      }
      QPainter painter(&frame);
      painter.setPen(Qt::white);
      QFont font = painter.font();
      font.setPixelSize(sourceSize.height() / 8);
      painter.setFont(font);
      painter.drawText(frame.rect(), Qt::AlignCenter,
                       QStringLiteral("Synthetic %1 / %2").arg(id + 1).arg(variant));
      mSources.push_back(std::move(frame));
    }
  }

  ~Tile() override { stop(); }

  void start() {
    mRunning.store(true);
    mWorker = std::thread([this] {
      auto deadline = Clock::now();
      const auto interval = std::chrono::nanoseconds(1000000000 / mFps);
      std::uint64_t sequence = 0;
      while (mRunning.load()) {
        // Match PR #94: one RGBA deep copy, latest frame under mutex, and
        // one queued repaint per delivered frame. The copy simulates the
        // ownership boundary after VideoStream::read(), not codec decoding.
        QImage image = mSources[sequence % mSources.size()].copy();
        {
          QMutexLocker locker(&mMutex);
          mLatest = std::move(image);
          mSequence = ++sequence;
          mDeliveredAt = Clock::now();
        }
        mDelivered.fetch_add(1);
        QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update),
                                  Qt::QueuedConnection);
        deadline += interval;
        std::this_thread::sleep_until(deadline);
      }
    });
  }

  void stop() {
    mRunning.store(false);
    if (mWorker.joinable()) {
      mWorker.join();
    }
  }

  void resetStats() {
    QMutexLocker locker(&mMutex);
    mDelivered.store(0);
    mPainted = 0;
    mLastPainted = mSequence;
    mPaintTimes.clear();
    mFrameAges.clear();
  }

  QJsonObject stats(double elapsed) const {
    return {{"id", mId}, {"delivered_frames", static_cast<qint64>(mDelivered.load())},
            {"painted_unique_frames", static_cast<qint64>(mPainted)},
            {"painted_fps", mPainted / elapsed},
            {"paint_ms_p95", percentile(mPaintTimes, .95)},
            {"frame_age_ms_p95", percentile(mFrameAges, .95)},
            {"renderer", mRenderer}, {"tile_width", width()}, {"tile_height", height()}};
  }

protected:
  void initializeGL() override {
    const auto *renderer = context()->functions()->glGetString(GL_RENDERER);
    mRenderer = renderer ? QString::fromLatin1(reinterpret_cast<const char *>(renderer))
                         : QStringLiteral("unknown");
  }

  void paintGL() override {
    const auto start = Clock::now();
    QImage frame;
    std::uint64_t sequence;
    Clock::time_point deliveredAt;
    {
      QMutexLocker locker(&mMutex);
      frame = mLatest;
      sequence = mSequence;
      deliveredAt = mDeliveredAt;
    }
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame.isNull()) {
      return;
    }
    const QSize scaledSize = frame.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaledSize.width()) / 2,
                             (height() - scaledSize.height()) / 2), scaledSize);
    if (mCpuScale) {
      // Current RemoteVideoRenderer::paintGL() path, PR #94 head 1212cd3.
      const QImage scaled = frame.scaled(target.size(), Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
      painter.drawImage(target.topLeft(), scaled);
    } else {
      // Let QPainter's OpenGL paint engine scale during texture rendering.
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, frame);
    }
    painter.end();
    const auto end = Clock::now();
    mPaintTimes.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    if (sequence != mLastPainted) {
      ++mPainted;
      mLastPainted = sequence;
      mFrameAges.push_back(std::chrono::duration<double, std::milli>(end - deliveredAt).count());
    }
  }

private:
  int mId;
  int mFps;
  bool mCpuScale;
  std::vector<QImage> mSources;
  QMutex mMutex;
  QImage mLatest;
  std::uint64_t mSequence = 0;
  Clock::time_point mDeliveredAt;
  std::atomic<bool> mRunning{false};
  std::atomic<std::uint64_t> mDelivered{0};
  std::thread mWorker;
  std::uint64_t mPainted = 0;
  std::uint64_t mLastPainted = 0;
  QString mRenderer;
  std::vector<double> mPaintTimes;
  std::vector<double> mFrameAges;
};

int main(int argc, char **argv) {
  // Keep process/device setup outside the measurement. Disable swap throttling
  // to measure throughput; actual rendering/compositing is still required.
  QSurfaceFormat format;
  format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOptions({{"streams", "Synthetic stream count (1-10)", "count", "10"},
                     {"width", "Source width", "pixels", "1280"},
                     {"height", "Source height", "pixels", "720"},
                     {"fps", "Source frame rate", "fps", "30"},
                     {"seconds", "Measurement duration", "seconds", "10"},
                     {"warmup", "Warmup duration", "seconds", "2"},
                     {"mode", "cpu-scale or painter-scale", "mode", "cpu-scale"},
                     {"gpu-metric", "Linux gpu_busy_percent path (system-wide)", "path"},
                     {"screenshot", "Save synthetic tile screenshot before measurement", "path"}});
  parser.process(app);
  const int streams = parser.value("streams").toInt();
  const QSize sourceSize(parser.value("width").toInt(), parser.value("height").toInt());
  const int fps = parser.value("fps").toInt();
  const int seconds = parser.value("seconds").toInt();
  const int warmup = parser.value("warmup").toInt();
  const QString mode = parser.value("mode");
  if (streams < 1 || streams > 10 || sourceSize.width() < 1 || sourceSize.height() < 1 ||
      sourceSize.width() > 3840 || sourceSize.height() > 2160 || fps < 1 || fps > 120 ||
      seconds < 1 || seconds > 300 || warmup < 0 || warmup > 60 ||
      (mode != "cpu-scale" && mode != "painter-scale")) {
    std::fprintf(stderr, "Invalid benchmark options\n");
    return 2;
  }
  QWidget window;
  window.setWindowTitle(QStringLiteral("Sessio #95 — synthetic rendering benchmark"));
  window.setFixedSize(1280, 800);
  auto *layout = new QGridLayout(&window);
  const int columns = static_cast<int>(std::ceil(std::sqrt(streams)));
  std::vector<Tile *> tiles;
  for (int id = 0; id < streams; ++id) {
    auto *tile = new Tile(id, sourceSize, fps, mode == "cpu-scale", &window);
    tile->setMinimumSize(160, 90);
    layout->addWidget(tile, id / columns, id % columns);
    tiles.push_back(tile);
  }
  window.show();
  for (auto *tile : tiles) {
    tile->start();
  }
  bool measuring = false;
  Clock::time_point start;
  Clock::time_point previousHeartbeat = Clock::now();
  double startCpu = 0;
  std::vector<double> heartbeatDelays;
  std::vector<double> gpuBusy;
  std::vector<double> resident;
  QTimer heartbeat;
  heartbeat.setTimerType(Qt::PreciseTimer);
  QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
    const auto now = Clock::now();
    if (measuring) {
      heartbeatDelays.push_back(std::max(0.0,
          std::chrono::duration<double, std::milli>(now - previousHeartbeat).count() - 16));
    }
    previousHeartbeat = now;
  });
  heartbeat.start(16);
  QTimer sampler;
  sampler.setTimerType(Qt::PreciseTimer);
  QObject::connect(&sampler, &QTimer::timeout, &app, [&] {
    if (measuring) {
      const double gpu = readMetric(parser.value("gpu-metric"));
      if (gpu >= 0) {
        gpuBusy.push_back(gpu);
      }
      const double rss = residentMiB();
      if (rss >= 0) {
        resident.push_back(rss);
      }
    }
  });
  sampler.start(100);
  QTimer::singleShot(warmup * 1000, &app, [&] {
    if (!parser.value("screenshot").isEmpty()) {
      window.grab().save(parser.value("screenshot"));
    }
    for (auto *tile : tiles) {
      tile->resetStats();
    }
    previousHeartbeat = Clock::now();
    startCpu = cpuSeconds();
    start = Clock::now();
    measuring = true;
    QTimer::singleShot(seconds * 1000, Qt::PreciseTimer, &app, [&] {
      const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
      const double cpu = cpuSeconds() - startCpu;
      measuring = false;
      QJsonArray tileStats;
      bool rendered = true;
      for (auto *tile : tiles) {
        const auto stats = tile->stats(elapsed);
        rendered &= stats.value("painted_unique_frames").toInteger() > 0 &&
                    !stats.value("renderer").toString().isEmpty();
        tileStats.append(stats);
      }
      for (auto *tile : tiles) {
        tile->stop();
      }
      const QJsonObject result{
          {"prototype", true}, {"valid_rendering", rendered}, {"streams", streams},
          {"mode", mode}, {"source_width", sourceSize.width()},
          {"source_height", sourceSize.height()}, {"target_fps", fps},
          {"measurement_seconds", elapsed}, {"process_cpu_percent_one_core", cpu / elapsed * 100},
          {"rss_mib_peak_sample", percentile(resident, 1)},
          {"source_pool_mib", streams * 3.0 * sourceSize.width() * sourceSize.height() * 4 / 1048576},
          {"gui_timer_delay_ms_p95", percentile(heartbeatDelays, .95)},
          {"gui_timer_delay_ms_max", percentile(heartbeatDelays, 1)},
          {"gpu_busy_percent_systemwide_p50", percentile(gpuBusy, .5)},
          {"gpu_busy_percent_systemwide_p95", percentile(gpuBusy, .95)},
          {"qt", qVersion()}, {"platform", QApplication::platformName()},
          {"window_width", window.width()}, {"window_height", window.height()},
          {"device_pixel_ratio", window.devicePixelRatioF()},
          {"kernel", QSysInfo::kernelVersion()}, {"tiles", tileStats}};
      const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
      std::fwrite(json.constData(), 1, json.size(), stdout);
      std::fputc('\n', stdout);
      app.exit(rendered ? 0 : 3);
    });
  });
  return app.exec();
}
