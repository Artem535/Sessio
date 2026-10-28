// Joins a Jitsi room through lib-jitsi-meet, which is loaded from the Jitsi
// server itself (its config.js names the deployment's XMPP domains).
// Emits the same handler calls as fake-adapter.js.

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(`cannot load ${src}`));
    document.head.appendChild(s);
  });
}

export async function createConference(options, handlers) {
  const server = new URL(options.serverUrl);
  const room = String(options.room || '').toLowerCase();
  if (!room) throw new Error('room is required');
  const wsScheme = server.protocol === 'http:' ? 'ws' : 'wss';

  await loadScript(new URL('/config.js', server).href);
  await loadScript(new URL('/libs/lib-jitsi-meet.min.js', server).href);
  const J = window.JitsiMeetJS;
  J.init({});
  J.setLogLevel(J.logLevels.WARN);

  const hosts = (window.config && window.config.hosts) || { domain: server.host, muc: `conference.${server.host}` };
  const connection = new J.JitsiConnection(null, options.jwt || null, {
    hosts,
    serviceUrl: `${wsScheme}://${server.host}/xmpp-websocket?room=${room}`,
  });

  let joined = false;
  let closed = false;
  let localId = null;
  let conference = null;
  const localTracks = { audio: null, video: null };

  // One failure path: before the conference is joined it is a join failure,
  // afterwards a lost connection.
  const fail = (reason) => {
    if (closed) return;
    if (joined) handlers.onConnectionLost(reason);
    else handlers.onFailed(reason);
  };

  // Failures while connecting reject (the caller reports a join failure);
  // later ones go through fail().
  let established = false;
  await new Promise((resolve, reject) => {
    connection.addEventListener(J.events.connection.CONNECTION_ESTABLISHED, () => {
      established = true;
      resolve();
    });
    connection.addEventListener(J.events.connection.CONNECTION_FAILED, (e, m) => {
      const reason = `connection failed: ${e} ${m || ''}`.trim();
      if (established) fail(reason);
      else reject(new Error(reason));
    });
    connection.addEventListener(J.events.connection.CONNECTION_DISCONNECTED, () => fail('connection disconnected'));
    connection.connect();
  });

  conference = connection.initJitsiConference(room, {});

  const emitTrack = (track, isLocal) => {
    const kind = track.getType();
    if (kind !== 'audio' && kind !== 'video') return null;
    const participantId = isLocal ? localId : track.getParticipantId();
    const participant = isLocal ? null : conference.getParticipantById(participantId);
    const displayName = isLocal ? options.displayName : (participant && participant.getDisplayName()) || participantId;
    return { participantId, displayName, isLocal, kind, track, stream: track.getOriginalStream(), audible: !isLocal };
  };

  conference.on(J.events.conference.TRACK_ADDED, (track) => {
    if (track.isLocal()) return;
    const ev = emitTrack(track, false);
    if (ev) handlers.onTrackAdded(ev);
  });
  conference.on(J.events.conference.TRACK_REMOVED, (track) => {
    if (track.isLocal()) return;
    const kind = track.getType();
    if (kind !== 'audio' && kind !== 'video') return;
    handlers.onTrackRemoved({ participantId: track.getParticipantId(), kind, track });
  });
  conference.on(J.events.conference.USER_LEFT, (id) => handlers.onParticipantLeft(id));
  conference.on(J.events.conference.DISPLAY_NAME_CHANGED, (id, name) => handlers.onDisplayName(id, name));
  conference.on(J.events.conference.CONFERENCE_FAILED, (err) => fail(`conference failed: ${err}`));
  conference.on(J.events.conference.KICKED, () => fail('kicked'));
  conference.on(J.events.conference.CONFERENCE_JOINED, () => {
    joined = true;
    localId = conference.myUserId();
    handlers.onJoined(localId);
    for (const track of Object.values(localTracks)) {
      if (!track) continue;
      const ev = emitTrack(track, true);
      if (ev) handlers.onTrackAdded(ev);
    }
  });
  conference.setDisplayName(options.displayName || '');

  // Missing devices must not block the call: it continues receive-only.
  async function createLocal(kind) {
    try {
      const [track] = await J.createLocalTracks({ devices: [kind] });
      return track;
    } catch (err) {
      handlers.onMediaError(`${kind}: ${err && err.message ? err.message : err}`);
      return null;
    }
  }

  async function attach(kind) {
    const track = await createLocal(kind);
    if (!track) return;
    localTracks[kind] = track;
    await conference.addTrack(track);
    // Added after the conference was joined: announce it ourselves.
    if (joined) {
      const ev = emitTrack(track, true);
      if (ev) handlers.onTrackAdded(ev);
    }
  }

  if (options.startWithMicrophone !== false) await attach('audio');
  if (options.startWithCamera !== false) await attach('video');
  conference.join();

  async function setEnabled(kind, enabled) {
    const track = localTracks[kind];
    if (!track) {
      if (enabled) await attach(kind);
      return;
    }
    await (enabled ? track.unmute() : track.mute());
  }

  return {
    async leave() {
      closed = true;
      for (const track of Object.values(localTracks)) {
        if (track) await track.dispose().catch(() => {});
      }
      try {
        await conference.leave();
      } finally {
        await connection.disconnect().catch(() => {});
      }
    },
    setMicrophoneEnabled: (enabled) => setEnabled('audio', enabled),
    setCameraEnabled: (enabled) => setEnabled('video', enabled),
  };
}
