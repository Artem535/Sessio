#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QWidget>

namespace pcm::calls {

// Loudest sample in `pcm`, scaled to 0..1, for a meter. Understands the sample formats a capture
// device may report; anything else reads as silence.
[[nodiscard]] double normalizedPeak(const QByteArray &pcm, QAudioFormat::SampleFormat format);

// A short sine tone in `format`'s own sample format and layout (every channel carries it), for
// the "Test" button next to the speaker selector. Empty if the format is not one we can write.
[[nodiscard]] QByteArray makeTestTone(const QAudioFormat &format, int durationMs, double frequencyHz);

// Segmented bar showing a live microphone level.
class MicLevelMeter final : public QWidget {
  Q_OBJECT

public:
  explicit MicLevelMeter(QWidget *parent = nullptr);

  // Rises immediately, falls gradually, so brief peaks stay readable.
  void setLevel(double level);
  void reset();
  [[nodiscard]] double level() const { return mLevel; }
  [[nodiscard]] int litSegments() const;

  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  static constexpr int kSegments = 12;
  double mLevel{0.0};
};

} // namespace pcm::calls
