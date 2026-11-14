#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "phrase_segmenter.h"
#include "resampler.h"
#include "speech_recognizer.h"
#include "transcription_types.h"
#include "voice_activity.h"

namespace pcm::transcription {

struct EngineCallbacks {
  std::function<void(const TranscribedPhrase&)> on_phrase;  // decode worker thread
  std::function<void(bool delayed)> on_delayed;             // decode worker thread, on change only
};

struct EngineConfig {
  std::chrono::milliseconds delayed_after{10000};
};

struct EngineStats {
  uint64_t phrases = 0;
  uint64_t decode_failures = 0;
  uint64_t dropped_on_stop = 0;
  uint64_t track_failures = 0;
  uint64_t callback_failures = 0;  // on_phrase/on_delayed threw; the phrase is lost
  size_t queued = 0;
  bool delayed = false;
};

// Real-time transcription of several audio tracks with one shared recogniser.
// Threads: callers of pushAudio only copy samples; one segmenter thread
// resamples and runs every track's VAD; one decode worker runs the recogniser.
class TranscriptionEngine {
 public:
  using VadFactory = std::function<std::unique_ptr<IVoiceActivityDetector>()>;

  TranscriptionEngine(VadFactory vad_factory, std::shared_ptr<ISpeechRecognizer> recognizer,
                      EngineCallbacks callbacks, EngineConfig config = {});
  ~TranscriptionEngine();

  TranscriptionEngine(const TranscriptionEngine&) = delete;
  TranscriptionEngine& operator=(const TranscriptionEngine&) = delete;

  // Calls the VAD factory on the calling thread; exceptions from the factory
  // propagate to the caller and no track is added. A duplicate id is ignored,
  // including a re-add right after removeTrack() until the segmenter thread has
  // erased the old track.
  void addTrack(const TrackInfo& info, int64_t start_offset_ms);
  // Flushes the track's open phrase. Later audio for it is ignored.
  void removeTrack(const TrackId& id);
  // Ordered with PCM in the track queue. Flushes the old phrase and starts a
  // fresh VAD/resampler at call time without replacing the participant track.
  void resetTrack(const TrackId& id, int64_t start_offset_ms);
  // Unknown, removed, or post-stop tracks are ignored. The first sample rate
  // seen for a track wins.
  void pushAudio(const TrackId& id, const int16_t* samples, size_t count, int sample_rate);
  // Stops accepting audio, processes what is queued, waits up to drain_timeout
  // for decoding, drops the rest (counted). Idempotent. Must not be called from
  // inside a callback (it would join the calling thread).
  void stop(std::chrono::milliseconds drain_timeout = std::chrono::seconds(5));

  EngineStats stats() const;

 private:
  using Clock = std::chrono::steady_clock;

  struct Input {
    std::vector<int16_t> samples;
    int rate = 0;
    std::optional<int64_t> reset_offset;
  };

  struct Track {
    TrackInfo info;
    int64_t start_offset_ms = 0;
    // Guarded by mutex_:
    std::deque<Input> pending;
    int rate = 0;
    bool closing = false;
    // Segmenter thread only:
    bool failed = false;
    std::unique_ptr<Resampler> resampler;
    std::unique_ptr<PhraseSegmenter> segmenter;
  };

  struct Job {
    TrackInfo info;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::vector<float> samples;
    Clock::time_point queued;
  };

  struct Work {
    std::shared_ptr<Track> track;
    Input input;
  };

  void segmenterLoop();
  void decodeLoop();
  void process(Track& track, const std::vector<int16_t>& samples, int rate);
  void flush(Track& track);
  void enqueue(const Track& track, SpeechSegment&& segment);

  VadFactory vad_factory_;
  std::shared_ptr<ISpeechRecognizer> recognizer_;
  EngineCallbacks callbacks_;
  EngineConfig config_;

  mutable std::mutex mutex_;  // tracks_, per-track pending state, stopping_
  std::condition_variable cv_;
  std::map<TrackId, std::shared_ptr<Track>> tracks_;
  bool stopping_ = false;

  mutable std::mutex queue_mutex_;  // jobs_, flags below
  std::condition_variable queue_cv_;
  std::condition_variable idle_cv_;
  std::deque<Job> jobs_;
  bool segmenter_done_ = false;
  bool abort_ = false;
  bool decoding_ = false;
  bool delayed_ = false;

  std::atomic<uint64_t> phrases_{0};
  std::atomic<uint64_t> decode_failures_{0};
  std::atomic<uint64_t> dropped_on_stop_{0};
  std::atomic<uint64_t> track_failures_{0};
  std::atomic<uint64_t> callback_failures_{0};

  std::thread segmenter_thread_;
  std::thread decode_thread_;
  bool stopped_ = false;  // guarded by mutex_
};

}  // namespace pcm::transcription
