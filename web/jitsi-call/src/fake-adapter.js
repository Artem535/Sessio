// Network-free stand-in for jitsi-adapter: tone "participants" so the whole
// page (taps, WebSocket, bridge events) can be exercised offline.
// Membership: onUserJoined({participantId, displayName, isLocal}) / onParticipantLeft(id).
// Track events: {participantId, displayName, isLocal, kind, track, stream, audible}.

function createTones() {
  const ctx = new AudioContext();
  return {
    close: () => ctx.close().catch(() => {}),
    stream(index) {
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
    },
  };
}

export async function createConference(options, handlers) {
  const fake = options.fake || {};
  const count = Math.max(0, fake.participants ?? 2);
  const churnMs = fake.churnMs || 0;
  const localId = 'fake-local';
  const tones = createTones();
  const timers = new Set();
  const live = []; // {participantId, track, tone}
  const names = new Map();
  let nextTrack = 0;
  let closed = false;
  let micEnabled = true;

  const later = (fn, ms) => {
    const t = setTimeout(() => {
      timers.delete(t);
      if (!closed) fn();
    }, ms);
    timers.add(t);
  };

  function addAudio(participantId, isLocal, index) {
    const tone = tones.stream(index);
    const track = { id: `fake-track-${nextTrack++}` };
    live.push({ participantId, track, tone });
    handlers.onTrackAdded({ participantId, displayName: names.get(participantId), isLocal, kind: 'audio', track, stream: tone.stream, audible: false });
  }

  function removeAudio(track) {
    const i = live.findIndex((l) => l.track === track);
    if (i < 0) return;
    const [gone] = live.splice(i, 1);
    handlers.onTrackRemoved({ participantId: gone.participantId, kind: 'audio', track });
    gone.tone.stop();
  }

  function join(participantId, displayName, isLocal, index) {
    names.set(participantId, displayName);
    handlers.onUserJoined({ participantId, displayName, isLocal });
    addAudio(participantId, isLocal, index);
  }

  // Next tick so the caller has the controller before events arrive.
  later(() => {
    handlers.onJoined(localId);
    join(localId, options.displayName || 'Local', true, 0);
    for (let i = 1; i <= count; i++) join(`fake-${i}`, `Fake ${i}`, false, i);
    if (churnMs > 0 && count > 0) {
      // Remove first, re-add shortly after (what a P2P<->JVB switch does live):
      // the participant must stay joined throughout.
      const churn = () => {
        const old = live.find((l) => l.participantId === 'fake-1');
        if (old) removeAudio(old.track);
        later(() => addAudio('fake-1', false, 1), 50);
        later(churn, churnMs);
      };
      later(churn, churnMs);
    }
  }, 0);

  return {
    leave() {
      closed = true;
      timers.forEach(clearTimeout);
      timers.clear();
      live.splice(0).forEach((l) => l.tone.stop());
      return tones.close();
    },
    setMicrophoneEnabled(enabled) {
      micEnabled = !!enabled;
    },
    isMicrophoneEnabled: () => micEnabled,
    setCameraEnabled() {},
  };
}
