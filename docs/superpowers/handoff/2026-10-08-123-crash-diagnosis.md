# Issue 123: capture diagnosis

Base: `77bbf5e`, `feat/118-call-transcription`; isolated branch
`codex/123-microphone-switch`. Host: Fedora 44, Qt 6.11.2, LiveKit 1.12.0.

## Proven regressions

`AudioCaptureLifecycle.StopDuringFrameDeliveryDiscardsOldBatch` injects a
synthetic QBuffer containing 20 silence frames into the real capture reader and
LiveKit AudioSource. A direct frame callback stops capture after frame 1. Baseline
still delivers all 20 frames. This proves that stop/start does not cancel a batch
already read from the old source. Clang ASan/UBSan reproduces the failed assertion;
it does not report a memory violation in that stop-only test.

A second baseline harness uses the same synthetic batch and destroys the adapter
inside its first direct `frameCaptured` callback. Clang ASan reports
`heap-use-after-free`: `std::__shared_ptr<livekit::AudioSource>::get()` called by
`AudioCaptureAdapter::onReadyRead`, baseline line 56, after the line-58 signal
deleted the adapter. The baseline source is extracted from `77bbf5e` into the
ignored local harness; only a friend test-access declaration is added to bind the
synthetic QIODevice. This establishes a real reentrant capture lifetime bug, but
does not establish that the user's physical-device switch follows this path.
The committed regression exercises the source factory and real readyRead
connection. A QPointer guard ends delivery after destruction.

`TranscriptionEngineTest.ResetSeparatesSpeechAndPreservesRemoteTrack` supplies
synthetic speech before and after a capture gap plus continuous remote speech.
Without a reset operation the local halves form one phrase: 2 phrases instead of
3. The current session clock also begins at transcription start, rather than call
join. These independent defects justify a narrow old-batch cancellation and
ordered track reset, without replacing the engine or the published source.

## Pending original crash reproduction

The host has one physical audio input. No original active-call A/B hardware crash
trace has been captured. The regressions above are not claimed as proof of the
user's original crash cause. Two-microphone original-OS and 30-minute remote-call
QA remain release gates. No actual microphone audio or participant data are stored
in test artifacts.

## Implementation ruling

Ruling: implement the independently demonstrated lifecycle/discontinuity defects
while retaining the original crash gate. Guard generations before backend stop,
disconnect old callbacks, reject already-read old PCM after reentrant transitions.
Keep LiveKit AudioSource and participant/transcript identity. Queue the reset in
the same per-track input queue as PCM so a worker scheduling race cannot mix the
two sides of the gap. This does not establish a fix for an unknown driver hang.
