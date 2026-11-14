#include "transcription_engine.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace pcm::transcription {

namespace {

std::string trim(std::string text) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

int64_t samplesToMs(int64_t samples) { return samples * 1000 / kRecognizerSampleRate; }

}  // namespace

TranscriptionEngine::TranscriptionEngine(VadFactory vad_factory,
                                         std::shared_ptr<ISpeechRecognizer> recognizer,
                                         EngineCallbacks callbacks, EngineConfig config)
    : vad_factory_(std::move(vad_factory)),
      recognizer_(std::move(recognizer)),
      callbacks_(std::move(callbacks)),
      config_(config) {
  segmenter_thread_ = std::thread([this] { segmenterLoop(); });
  decode_thread_ = std::thread([this] { decodeLoop(); });
}

TranscriptionEngine::~TranscriptionEngine() { stop(std::chrono::milliseconds(0)); }

void TranscriptionEngine::addTrack(const TrackInfo& info, int64_t start_offset_ms) {
  auto track = std::make_shared<Track>();
  track->info = info;
  track->start_offset_ms = start_offset_ms;
  track->segmenter = std::make_unique<PhraseSegmenter>(vad_factory_());
  std::lock_guard lock(mutex_);
  if (stopping_ || tracks_.count(info.id)) return;
  tracks_.emplace(info.id, std::move(track));
}

void TranscriptionEngine::removeTrack(const TrackId& id) {
  {
    std::lock_guard lock(mutex_);
    const auto it = tracks_.find(id);
    if (it == tracks_.end()) return;
    it->second->closing = true;
  }
  cv_.notify_one();
}

void TranscriptionEngine::pushAudio(const TrackId& id, const int16_t* samples, size_t count,
                                    int sample_rate) {
  if (count == 0) return;
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    const auto it = tracks_.find(id);
    if (it == tracks_.end() || it->second->closing) return;
    Track& track = *it->second;
    if (track.rate == 0) track.rate = sample_rate;
    if (track.rate != sample_rate) return;
    if (track.pending.empty() || track.pending.back().reset_offset)
      track.pending.push_back({{}, sample_rate, std::nullopt});
    auto &pending = track.pending.back().samples;
    pending.insert(pending.end(), samples, samples + count);
  }
  cv_.notify_one();
}

void TranscriptionEngine::resetTrack(const TrackId& id, int64_t start_offset_ms) {
  {
    std::lock_guard lock(mutex_);
    const auto it = tracks_.find(id);
    if (stopping_ || it == tracks_.end() || it->second->closing) return;
    it->second->pending.push_back({{}, 0, start_offset_ms});
    it->second->rate = 0;
  }
  cv_.notify_one();
}

void TranscriptionEngine::stop(std::chrono::milliseconds drain_timeout) {
  {
    std::lock_guard lock(mutex_);
    if (stopped_) return;
    stopped_ = true;
    stopping_ = true;
  }
  cv_.notify_all();
  segmenter_thread_.join();

  {
    std::unique_lock lock(queue_mutex_);
    idle_cv_.wait_for(lock, drain_timeout, [this] { return jobs_.empty() && !decoding_; });
    if (!jobs_.empty() || decoding_) abort_ = true;
  }
  queue_cv_.notify_all();
  decode_thread_.join();
}

EngineStats TranscriptionEngine::stats() const {
  EngineStats s;
  s.phrases = phrases_;
  s.decode_failures = decode_failures_;
  s.dropped_on_stop = dropped_on_stop_;
  s.track_failures = track_failures_;
  s.callback_failures = callback_failures_;
  std::lock_guard lock(queue_mutex_);
  s.queued = jobs_.size();
  s.delayed = delayed_;
  return s;
}

void TranscriptionEngine::segmenterLoop() {
  for (;;) {
    std::vector<Work> work;
    std::vector<std::shared_ptr<Track>> finished;
    bool stopping = false;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] {
        if (stopping_) return true;
        for (const auto& [id, t] : tracks_) {
          if (!t->pending.empty() || t->closing) return true;
        }
        return false;
      });
      stopping = stopping_;
      for (auto it = tracks_.begin(); it != tracks_.end();) {
        Track& t = *it->second;
        while (!t.pending.empty()) {
          work.push_back({it->second, std::move(t.pending.front())});
          t.pending.pop_front();
        }
        if (t.closing || stopping) {
          finished.push_back(it->second);
          it = tracks_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (Work& w : work) {
      if (w.input.reset_offset) {
        flush(*w.track);
        try {
          w.track->segmenter = std::make_unique<PhraseSegmenter>(vad_factory_());
          w.track->resampler.reset();
          w.track->start_offset_ms = *w.input.reset_offset;
          w.track->failed = false;
        } catch (...) {
          w.track->failed = true;
          ++track_failures_;
        }
      } else {
        process(*w.track, w.input.samples, w.input.rate);
      }
    }
    for (auto& t : finished) flush(*t);
    if (stopping) break;
  }
  {
    std::lock_guard lock(queue_mutex_);
    segmenter_done_ = true;
  }
  queue_cv_.notify_all();
}

void TranscriptionEngine::process(Track& track, const std::vector<int16_t>& samples, int rate) {
  if (track.failed) return;
  try {
    if (!track.resampler) track.resampler = std::make_unique<Resampler>(rate);
    const std::vector<float> audio = track.resampler->process(samples);
    for (SpeechSegment& seg : track.segmenter->feed(audio)) enqueue(track, std::move(seg));
  } catch (...) {
    track.failed = true;  // the track stays silent; other tracks are unaffected
    ++track_failures_;
  }
}

void TranscriptionEngine::flush(Track& track) {
  if (track.failed) return;
  try {
    for (SpeechSegment& seg : track.segmenter->flush()) enqueue(track, std::move(seg));
  } catch (...) {
    track.failed = true;
    ++track_failures_;
  }
}

void TranscriptionEngine::enqueue(const Track& track, SpeechSegment&& segment) {
  Job job;
  job.info = track.info;
  job.start_ms = track.start_offset_ms + samplesToMs(segment.start_sample);
  job.end_ms = job.start_ms + samplesToMs(static_cast<int64_t>(segment.samples.size()));
  job.samples = std::move(segment.samples);
  job.queued = Clock::now();
  {
    std::lock_guard lock(queue_mutex_);
    jobs_.push_back(std::move(job));
  }
  queue_cv_.notify_one();
}

void TranscriptionEngine::decodeLoop() {
  for (;;) {
    Job job;
    bool delayed_now = false;
    bool delayed_changed = false;
    {
      std::unique_lock lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { return !jobs_.empty() || segmenter_done_ || abort_; });
      if (abort_) {
        dropped_on_stop_ += jobs_.size();
        jobs_.clear();
        break;
      }
      if (jobs_.empty()) {
        if (segmenter_done_) break;
        continue;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
      decoding_ = true;
      delayed_now = Clock::now() - job.queued > config_.delayed_after;
      delayed_changed = delayed_now != delayed_;
      delayed_ = delayed_now;
    }
    if (delayed_changed && callbacks_.on_delayed) {
      try {
        callbacks_.on_delayed(delayed_now);
      } catch (...) {
        ++callback_failures_;
      }
    }

    std::string text;
    bool decoded = false;
    try {
      text = trim(recognizer_->transcribe(job.samples));
      decoded = true;
    } catch (...) {
      ++decode_failures_;
    }
    if (decoded && !text.empty()) {
      TranscribedPhrase phrase{job.info.id, job.info.role, job.info.display_name,
                               job.start_ms, job.end_ms, text};
      if (callbacks_.on_phrase) {
        try {
          callbacks_.on_phrase(phrase);
          ++phrases_;
        } catch (...) {
          ++callback_failures_;
        }
      } else {
        ++phrases_;
      }
    }

    bool clear_delayed = false;
    {
      std::lock_guard lock(queue_mutex_);
      decoding_ = false;
      if (jobs_.empty() && delayed_) {
        delayed_ = false;
        clear_delayed = true;
      }
    }
    idle_cv_.notify_all();
    if (clear_delayed && callbacks_.on_delayed) {
      try {
        callbacks_.on_delayed(false);
      } catch (...) {
        ++callback_failures_;
      }
    }
  }
  {
    std::lock_guard lock(queue_mutex_);
    decoding_ = false;
  }
  idle_cv_.notify_all();
}

}  // namespace pcm::transcription
