// Spike page: produce per-participant audio, tap it with an AudioWorklet and
// ship the chunks to C++ over the transport chosen in the query string.
const params = new URLSearchParams(location.search);
const transportName = params.get('transport');
const mode = params.get('mode');
const trackCount = Number(params.get('tracks') || 3);
const chunkMs = Number(params.get('chunkMs') || 20);
const SAMPLE_RATE = 16000;
const chunkSamples = (SAMPLE_RATE * chunkMs) / 1000;

const produced = {};
const page = { errors: 0, maxInflight: 0, maxWsBuffered: 0, tracksAdded: 0, tracksRemoved: 0 };
window.spikeProduced = () => ({ tracks: produced, page: { ...page, ctxState: ctx && ctx.state, ctxRate: ctx && ctx.sampleRate } });

const log = (...a) => {
  console.log(a.join(' '));
  document.getElementById('log').textContent += a.join(' ') + '\n';
};

// ---- transports: each exposes send(Uint8Array) ----

function base64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

async function channelTransport() {
  const bridge = await new Promise((resolve) => new QWebChannel(qt.webChannelTransport, (ch) => resolve(ch.objects.bridge)));
  return { send: (bytes) => bridge.push(base64(bytes)) };
}

async function wsTransport() {
  const ws = new WebSocket(`ws://127.0.0.1:${params.get('wsPort')}/?token=${params.get('token')}`);
  ws.binaryType = 'arraybuffer';
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = () => reject(new Error(`cannot load ${src}`));
  });
  ws.onclose = () => log('ws closed');
  return {
    send: (bytes) => {
      ws.send(bytes);
      page.maxWsBuffered = Math.max(page.maxWsBuffered, ws.bufferedAmount);
    },
  };
}

async function schemeTransport() {
  let inflight = 0;
  return {
    send: (bytes) => {
      inflight++;
      page.maxInflight = Math.max(page.maxInflight, inflight);
      fetch('sessio-audio://app/push', { method: 'POST', body: bytes })
        .catch(() => page.errors++)
        .finally(() => inflight--);
    },
  };
}

// ---- tapping ----

let ctx;
let transport;
let sink;
const encoder = new TextEncoder();

function tap(trackId, stream) {
  const src = ctx.createMediaStreamSource(stream);
  const node = new AudioWorkletNode(ctx, 'pcm-tap', {
    processorOptions: { chunkSamples },
    channelCount: 1,
    channelCountMode: 'explicit',
    channelInterpretation: 'speakers',
  });
  src.connect(node);
  node.connect(sink);
  const id = encoder.encode(trackId);
  let seq = 0;
  produced[trackId] = 0;
  node.port.onmessage = (e) => {
    const pcm = new Uint8Array(e.data);
    const blob = new Uint8Array(16 + id.length + pcm.length);
    const dv = new DataView(blob.buffer);
    dv.setUint8(0, 1);
    dv.setUint8(1, id.length);
    dv.setUint32(4, seq++, true);
    dv.setFloat64(8, performance.timeOrigin + performance.now(), true);
    blob.set(id, 16);
    blob.set(pcm, 16 + id.length);
    produced[trackId] = seq;
    try {
      transport.send(blob);
    } catch (err) {
      page.errors++;
    }
  };
  page.tracksAdded++;
  return () => {
    node.port.onmessage = null;
    src.disconnect();
    node.disconnect();
    page.tracksRemoved++;
  };
}

// A speech-like test signal: a tone per track, amplitude-modulated at ~3 Hz.
function toneStream(index) {
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
  return dest.stream;
}

// ---- modes ----

function runSynthetic() {
  for (let i = 0; i < trackCount; i++) tap(`p${i}`, toneStream(i));
}

// Real remote WebRTC tracks (Opus encode/decode, jitter buffer) without a server.
async function runLoopback() {
  const a = new RTCPeerConnection();
  const b = new RTCPeerConnection();
  a.onicecandidate = (e) => e.candidate && b.addIceCandidate(e.candidate);
  b.onicecandidate = (e) => e.candidate && a.addIceCandidate(e.candidate);
  const names = {};
  for (let i = 0; i < trackCount; i++) {
    const stream = toneStream(i);
    names[stream.id] = `p${i}`;
    a.addTrack(stream.getAudioTracks()[0], stream);
  }
  b.ontrack = (e) => {
    const stream = e.streams[0];
    // Chromium only pulls remote audio into WebAudio when a media element plays it.
    const el = new Audio();
    el.muted = true;
    el.srcObject = stream;
    el.play().catch(() => {});
    tap(names[stream.id] || stream.id, stream);
  };
  await a.setLocalDescription();
  await b.setRemoteDescription(a.localDescription);
  await b.setLocalDescription();
  await a.setRemoteDescription(b.localDescription);
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(`cannot load ${src}`));
    document.head.appendChild(s);
  });
}

async function runJitsi() {
  const domain = params.get('domain') || '';
  // Only a host name (and port) may pick the script origin.
  if (!/^[a-z0-9.-]+(:\d{1,5})?$/i.test(domain)) throw new Error('jitsi mode needs a valid --domain');
  const room = (params.get('room') || '').toLowerCase();
  if (!room) throw new Error('jitsi mode needs --room');
  // The deployment's config.js names its XMPP domains (docker-jitsi-meet uses
  // meet.jitsi / muc.meet.jitsi, not the public host name).
  await loadScript(`https://${domain}/config.js`);
  await loadScript(`https://${domain}/libs/lib-jitsi-meet.min.js`);
  const J = window.JitsiMeetJS;
  J.init({});
  J.setLogLevel(J.logLevels.WARN);

  const hosts = (window.config && window.config.hosts) || { domain, muc: `conference.${domain}` };
  const connection = new J.JitsiConnection(null, params.get('jwt') || null, {
    hosts,
    serviceUrl: `wss://${domain}/xmpp-websocket?room=${room}`,
  });
  await new Promise((resolve, reject) => {
    connection.addEventListener(J.events.connection.CONNECTION_ESTABLISHED, resolve);
    connection.addEventListener(J.events.connection.CONNECTION_FAILED, (e, m) => reject(new Error(`connection failed: ${e} ${m || ''}`)));
    connection.connect();
  });
  log('jitsi: connected');

  const conference = connection.initJitsiConference(room, {});
  const untap = new Map();
  conference.on(J.events.conference.TRACK_ADDED, (track) => {
    if (track.isLocal() || track.getType() !== 'audio') return;
    const pid = track.getParticipantId();
    const participant = conference.getParticipantById(pid);
    const name = (participant && participant.getDisplayName()) || pid;
    log(`jitsi: audio track from ${pid} (${name})`);
    const stream = track.getOriginalStream();
    const el = new Audio();
    el.muted = true;
    el.srcObject = stream;
    el.play().catch(() => {});
    untap.set(track, tap(`${pid}:${name}`, stream));
  });
  conference.on(J.events.conference.TRACK_REMOVED, (track) => {
    const stop = untap.get(track);
    if (stop) {
      stop();
      untap.delete(track);
      log(`jitsi: audio track removed from ${track.getParticipantId()}`);
    }
  });
  conference.on(J.events.conference.USER_JOINED, (id, user) => log(`jitsi: joined ${id} ${user.getDisplayName() || ''}`));
  conference.on(J.events.conference.USER_LEFT, (id) => log(`jitsi: left ${id}`));
  conference.on(J.events.conference.CONFERENCE_JOINED, () => log('jitsi: conference joined'));
  conference.on(J.events.conference.CONFERENCE_FAILED, (err) => log(`jitsi: conference failed ${err}`));
  conference.setDisplayName('Sessio spike');

  if (params.get('mic') === '1') {
    const [local] = await J.createLocalTracks({ devices: ['audio'] });
    await conference.addTrack(local);
    tap('local', local.getOriginalStream());
  }
  conference.join();
}

async function main() {
  ctx = new AudioContext({ sampleRate: SAMPLE_RATE });
  await ctx.audioWorklet.addModule('pcm-tap.js');
  await ctx.resume();
  sink = ctx.createGain();
  sink.gain.value = 0;
  sink.connect(ctx.destination);

  const none = async () => ({ send: () => {} });
  transport = await { channel: channelTransport, ws: wsTransport, scheme: schemeTransport, none }[transportName]();
  log(`ready: transport=${transportName} mode=${mode} rate=${ctx.sampleRate} state=${ctx.state}`);

  if (mode === 'synthetic') runSynthetic();
  else if (mode === 'loopback') await runLoopback();
  else if (mode === 'jitsi') await runJitsi();
  if (mode !== 'jitsi' && params.get('mic') === '1') {
    tap('local', await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true } }));
  }
}

main().catch((err) => {
  page.errors++;
  log(`fatal: ${err && err.stack ? err.stack : err}`);
});
