// Network-free stand-in for jitsi-adapter: tone "participants" so the whole
// page (taps, WebSocket, bridge events) can be exercised offline.
// Track events: {participantId, displayName, isLocal, kind, track, stream, audible}.

let toneCtx;

function toneStream(index) {
  toneCtx = toneCtx || new AudioContext();
  const ctx = toneCtx;
  const osc = ctx.createOscillator();
  osc.frequency.value = 220 + 110 * index;
  const lfo = ctx.createOscillator();
  lfo.frequency.value = 2.5 + index * 0.4;
  const depth = ctx.createGain();
  depth.gain.value = 0.25;
  const amp = ctx.createGain();
  amp.gain.value = 0.25;
  lfo.connect(depth).connect(amp.gain);
  const dest = ctx.createMediaStreamDestination();
  osc.connect(amp).connect(dest);
  osc.start();
  lfo.start();
  ctx.resume().catch(() => {});
  return {
    stream: dest.stream,
    stop() {
      try {
        osc.stop();
        lfo.stop();
      } catch (e) {
        // already stopped
      }
    },
  };
}

export async function createConference(options, handlers) {
  const fake = options.fake || {};
  const count = Math.max(0, fake.participants ?? 2);
  const churnMs = fake.churnMs || 0;
  const localId = 'fake-local';
  const timers = [];
  const live = []; // {participantId, track, tone}
  let nextTrack = 0;
  let micEnabled = true;

  function addAudio(participantId, displayName, isLocal, index) {
    const tone = toneStream(index);
    const track = { id: `fake-track-${nextTrack++}` };
    live.push({ participantId, track, tone });
    handlers.onTrackAdded({ participantId, displayName, isLocal, kind: 'audio', track, stream: tone.stream, audible: false });
    return track;
  }

  function removeAudio(participantId, track) {
    const i = live.findIndex((l) => l.track === track);
    if (i < 0) return;
    const [gone] = live.splice(i, 1);
    handlers.onTrackRemoved({ participantId, kind: 'audio', track });
    gone.tone.stop();
  }

  // Next tick so the caller has the controller (and joined handler order) first.
  setTimeout(() => {
    handlers.onJoined(localId);
    addAudio(localId, options.displayName || 'Local', true, 0);
    for (let i = 1; i <= count; i++) addAudio(`fake-${i}`, `Fake ${i}`, false, i);
    if (churnMs > 0 && count > 0) {
      // New track first, old one dropped shortly after: the order a P2P<->JVB
      // switch produces, so the stale removal must not look like a departure.
      timers.push(
        setInterval(() => {
          const old = live.find((l) => l.participantId === 'fake-1');
          if (!old) return;
          addAudio('fake-1', 'Fake 1', false, 1);
          timers.push(setTimeout(() => removeAudio('fake-1', old.track), 50));
        }, churnMs),
      );
    }
  }, 0);

  return {
    leave() {
      timers.forEach((t) => {
        clearInterval(t);
        clearTimeout(t);
      });
      live.splice(0).forEach((l) => l.tone.stop());
    },
    setMicrophoneEnabled(enabled) {
      micEnabled = !!enabled;
    },
    isMicrophoneEnabled: () => micEnabled,
    setCameraEnabled() {},
  };
}
