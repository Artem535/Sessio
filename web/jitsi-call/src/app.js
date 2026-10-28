import { encodeChunk } from './chunk.js';
import { ParticipantRegistry } from './participants.js';
import { copy } from './copy.js';

const SAMPLE_RATE = 16000;
const CHUNK_SAMPLES = SAMPLE_RATE / 50; // 20 ms
const WS_MAX_BUFFERED = 1 << 20; // drop audio rather than grow without bound

// ---- bridge to C++ (QWebChannel) ----

const bridge = await new Promise((resolve) => {
  if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
    console.warn('sessio-call: no QWebChannel, running without a host');
    resolve(null);
    return;
  }
  new QWebChannel(qt.webChannelTransport, (ch) => resolve(ch.objects.bridge));
});

function notify(name, ...args) {
  if (!bridge || typeof bridge[name] !== 'function') {
    console.log(`bridge.${name}`, ...args);
    return;
  }
  try {
    bridge[name](...args);
  } catch (err) {
    console.error(`bridge.${name} failed`, err);
  }
}

// ---- UI ----

const $ = (id) => document.getElementById(id);
const grid = $('grid');
const tiles = new Map(); // participantId -> {el, video, name}

function setStatus(key) {
  $('status').textContent = copy(key);
}

function ensureTile(id, name, isLocal) {
  let tile = tiles.get(id);
  if (!tile) {
    const el = document.createElement('div');
    el.className = `tile${isLocal ? ' local' : ''}`;
    const avatar = document.createElement('div');
    avatar.className = 'avatar';
    const label = document.createElement('div');
    label.className = 'name';
    el.append(avatar, label);
    grid.appendChild(el);
    tile = { el, label, avatar, video: null, videoTrack: null };
    tiles.set(id, tile);
  }
  setTileName(id, isLocal ? `${name || ''} (${copy('you')})`.trim() : name);
  return tile;
}

function setTileName(id, name) {
  const tile = tiles.get(id);
  if (!tile || !name) return;
  tile.label.textContent = name;
  tile.avatar.textContent = Array.from(name)[0] || '';
}

function attachVideo(id, stream, track) {
  const tile = tiles.get(id);
  if (!tile) return;
  detachVideo(id);
  const v = document.createElement('video');
  v.autoplay = true;
  v.playsInline = true;
  v.muted = true;
  v.srcObject = stream;
  v.play().catch(() => {});
  tile.el.prepend(v);
  tile.el.classList.add('has-video');
  tile.video = v;
  tile.videoTrack = track;
}

function detachVideo(id) {
  const tile = tiles.get(id);
  if (!tile || !tile.video) return;
  tile.video.srcObject = null;
  tile.video.remove();
  tile.video = null;
  tile.videoTrack = null;
  tile.el.classList.remove('has-video');
}

function removeTile(id) {
  detachVideo(id);
  const tile = tiles.get(id);
  if (tile) tile.el.remove();
  tiles.delete(id);
}

function clearUi() {
  for (const id of [...tiles.keys()]) removeTile(id);
}

// ---- audio taps: one AudioWorklet per participant, keyed by participant id ----

let registry = new ParticipantRegistry();
const peers = new Map(); // participantId -> {isLocal, audio: {track, stream, el}|null, tap: {node, src}|null}
let ctx = null;
let ctxReady = null;
let sink = null;
let captureOn = false;
let ws = null;

function ensureAudioContext() {
  if (!ctxReady) {
    ctxReady = (async () => {
      // 16 kHz context: the browser resamples, so C++ gets the rate it wants.
      const c = new AudioContext({ sampleRate: SAMPLE_RATE });
      await c.audioWorklet.addModule(new URL('./pcm-tap.js', import.meta.url).href);
      await c.resume();
      sink = c.createGain();
      sink.gain.value = 0; // keeps the graph pulled without playing the tap
      sink.connect(c.destination);
      ctx = c;
    })();
    ctxReady.catch((err) => {
      ctxReady = null;
      notify('mediaError', `audio tap: ${err && err.message ? err.message : err}`);
    });
  }
  return ctxReady;
}

function sendChunk(participantId, buffer) {
  if (!ws || ws.readyState !== WebSocket.OPEN || ws.bufferedAmount > WS_MAX_BUFFERED) return;
  try {
    ws.send(encodeChunk(participantId, registry.seq(participantId), performance.timeOrigin + performance.now(), buffer));
  } catch (err) {
    console.warn('sessio-call: chunk dropped', err);
  }
}

function startTap(id, peer) {
  if (peer.tap || !ctx) return;
  const node = new AudioWorkletNode(ctx, 'pcm-tap', {
    processorOptions: { chunkSamples: CHUNK_SAMPLES },
    channelCount: 1,
    channelCountMode: 'explicit',
    channelInterpretation: 'speakers',
  });
  node.connect(sink);
  node.port.onmessage = (e) => sendChunk(id, e.data);
  peer.tap = { node, src: null };
  bindTapSource(peer);
}

// Points the participant's tap at its current audio track (or silence).
function bindTapSource(peer) {
  if (!peer.tap) return;
  if (peer.tap.src) {
    peer.tap.src.disconnect();
    peer.tap.src = null;
  }
  if (peer.audio && peer.audio.stream.getAudioTracks().length) {
    peer.tap.src = ctx.createMediaStreamSource(peer.audio.stream);
    peer.tap.src.connect(peer.tap.node);
  }
}

function stopTap(peer) {
  if (!peer.tap) return;
  peer.tap.node.port.onmessage = null;
  if (peer.tap.src) peer.tap.src.disconnect();
  peer.tap.node.disconnect();
  peer.tap = null;
}

async function setAudioCapture(on) {
  captureOn = !!on;
  if (!captureOn) {
    peers.forEach(stopTap);
    return;
  }
  try {
    await ensureAudioContext();
  } catch (err) {
    return; // reported by ensureAudioContext
  }
  if (!captureOn) return; // switched off while the worklet was loading
  peers.forEach((peer, id) => startTap(id, peer));
}

function connectAudioSocket({ wsPort, wsToken }) {
  if (ws) {
    const old = ws;
    ws = null;
    old.close();
  }
  if (!wsPort) return;
  const socket = new WebSocket(`ws://127.0.0.1:${wsPort}/?token=${encodeURIComponent(wsToken || '')}`);
  socket.binaryType = 'arraybuffer';
  ws = socket;
  socket.onclose = () => {
    // A socket we replaced or closed ourselves has already been detached.
    if (ws !== socket) return;
    ws = null;
    notify('audioBridgeLost');
  };
}

// ---- conference lifecycle ----

let conference = null;
let joining = false;
let micOn = true;
let camOn = true;

function peerFor(id, isLocal) {
  let peer = peers.get(id);
  if (!peer) {
    peer = { isLocal, audio: null, tap: null };
    peers.set(id, peer);
  }
  return peer;
}

function forgetParticipant(id, notifyCpp) {
  const peer = peers.get(id);
  if (peer) {
    stopTap(peer);
    if (peer.audio && peer.audio.el) peer.audio.el.remove();
    peers.delete(id);
  }
  removeTile(id);
  if (notifyCpp) notify('participantLeft', id);
}

function announceParticipant(id, name, isLocal) {
  if (!registry.join(id)) return;
  peerFor(id, isLocal);
  ensureTile(id, name, isLocal);
  notify('participantJoined', id, name || id, !!isLocal);
}

// Terminal paths (join failure, lost connection, kicked) release everything,
// so a later join() works without C++ having to call leave() first.
const handlers = {
  onJoined(localId) {
    if (!joining) return;
    setStatus('connected');
    notify('joined', localId);
  },
  onFailed(reason) {
    if (!joining) return;
    teardown('failed');
    notify('joinFailed', String(reason));
  },
  onConnectionLost(reason) {
    if (!joining) return;
    teardown(reason === 'kicked' ? 'kicked' : 'lost');
    notify('connectionLost', String(reason));
  },
  onMediaError(reason) {
    notify('mediaError', String(reason));
  },
  onDisplayName(id, name) {
    setTileName(id, name);
  },
  onUserJoined({ participantId, displayName, isLocal }) {
    if (!joining) return;
    announceParticipant(participantId, displayName, isLocal);
  },
  onParticipantLeft(id) {
    if (registry.leave(id)) forgetParticipant(id, true);
  },
  onTrackAdded({ participantId, displayName, isLocal, kind, track, stream, audible }) {
    if (!joining) return; // late event from a call we already left
    announceParticipant(participantId, displayName, isLocal); // no-op when membership came first
    registry.addTrack(participantId, track);
    const peer = peerFor(participantId, isLocal);
    if (kind === 'video') {
      attachVideo(participantId, stream, track);
      return;
    }
    if (peer.audio && peer.audio.el) peer.audio.el.remove();
    let el = null;
    if (!isLocal) {
      // Chromium only feeds remote audio into WebAudio while an element plays
      // it; the element is muted when the host plays the call itself.
      el = document.createElement('audio');
      el.autoplay = true;
      el.muted = !audible;
      el.srcObject = stream;
      el.play().catch(() => {});
      grid.appendChild(el);
    }
    peer.audio = { track, stream, el };
    if (captureOn && ctx) {
      startTap(participantId, peer);
      bindTapSource(peer);
    }
  },
  // Tracks only drive the tap; presence is membership (see onParticipantLeft).
  onTrackRemoved({ participantId, kind, track }) {
    registry.removeTrack(participantId, track);
    const peer = peers.get(participantId);
    if (!peer) return;
    if (kind === 'audio' && peer.audio && peer.audio.track === track) {
      if (peer.audio.el) peer.audio.el.remove();
      peer.audio = null;
      bindTapSource(peer); // silence until the replacement track arrives
    }
    if (kind === 'video') {
      const tile = tiles.get(participantId);
      if (tile && tile.videoTrack === track) detachVideo(participantId);
    }
  },
};

async function join(options) {
  if (joining || conference) {
    console.warn('sessio-call: join ignored, already in a call');
    return;
  }
  joining = true;
  setStatus('connecting');
  if (options.wsPort) connectAudioSocket(options);
  try {
    const module = options.adapter === 'fake' ? await import('./fake-adapter.js') : await import('./jitsi-adapter.js');
    const created = await module.createConference(options, handlers);
    // leave() may have been called while connecting.
    if (!joining) {
      await created.leave();
      return;
    }
    conference = created;
  } catch (err) {
    handlers.onFailed(err && err.message ? err.message : err);
  }
}

async function teardown(statusKey) {
  const was = conference;
  joining = false;
  conference = null;
  peers.forEach(stopTap);
  for (const id of [...peers.keys()]) forgetParticipant(id, false);
  if (ws) {
    const old = ws;
    ws = null;
    old.close();
  }
  clearUi();
  setStatus(statusKey);
  registry = new ParticipantRegistry();
  try {
    if (was) await was.leave();
  } catch (err) {
    console.warn('sessio-call: leave failed', err);
  }
}

async function leave() {
  await teardown('left');
  notify('left');
}

function setMicrophoneEnabled(on) {
  micOn = !!on;
  $('mic').setAttribute('aria-pressed', String(micOn));
  if (conference) Promise.resolve(conference.setMicrophoneEnabled(micOn)).catch((e) => notify('mediaError', `audio: ${e && e.message ? e.message : e}`));
}

function setCameraEnabled(on) {
  camOn = !!on;
  $('cam').setAttribute('aria-pressed', String(camOn));
  if (conference) Promise.resolve(conference.setCameraEnabled(camOn)).catch((e) => notify('mediaError', `video: ${e && e.message ? e.message : e}`));
}

const api = {
  join,
  leave,
  setMicrophoneEnabled,
  setCameraEnabled,
  setAudioCapture,
  reconnectAudio: (opts) => connectAudioSocket(opts || {}),
};

$('mic').textContent = copy('mic');
$('cam').textContent = copy('camera');
$('leave').textContent = copy('leave');
$('mic').onclick = () => setMicrophoneEnabled(!micOn);
$('cam').onclick = () => setCameraEnabled(!camOn);
$('leave').onclick = () => leave();

// Replace the queueing stub from index.html and replay what arrived early.
const early = window.__sessioCallQueue || [];
window.sessioCall = api;
for (const [name, args] of early) {
  if (typeof api[name] === 'function') api[name](...args);
  else console.warn(`sessio-call: unknown early call ${name}`);
}
