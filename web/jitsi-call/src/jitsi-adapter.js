// Joins a Jitsi room through lib-jitsi-meet, which is loaded from the Jitsi
// server itself (its config.js names the deployment's XMPP domains).
// Emits the same handler calls as fake-adapter.js.

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error('Could not load the Jitsi server resources. Check the server address.'));
    document.head.appendChild(s);
  });
}

function deploymentBase(serverUrl) {
  const base = new URL(serverUrl);
  if (!base.pathname.endsWith('/')) base.pathname += '/';
  return base;
}

export function deploymentUrls(serverUrl) {
  const base = deploymentBase(serverUrl);
  return {
    config: new URL('config.js', base).href,
    library: new URL('libs/lib-jitsi-meet.min.js', base).href,
  };
}

export function connectionOptions(serverUrl, room, config = {}) {
  const base = deploymentBase(serverUrl);
  const hosts = config.hosts || { domain: base.hostname, muc: `conference.${base.hostname}` };
  const websocket = typeof config.websocket === 'string' && config.websocket.trim();
  const bosh = typeof config.bosh === 'string' && config.bosh.trim();
  const endpoint = new URL(websocket || bosh || 'xmpp-websocket', base);
  if (endpoint.username || endpoint.password) throw new Error('Jitsi connection addresses cannot contain credentials.');
  if (websocket || !bosh) {
    if (endpoint.protocol === 'https:') endpoint.protocol = 'wss:';
    else if (endpoint.protocol === 'http:') endpoint.protocol = 'ws:';
    if (!['ws:', 'wss:'].includes(endpoint.protocol)) throw new Error('Invalid Jitsi websocket address.');
  } else if (!['http:', 'https:'].includes(endpoint.protocol)) {
    throw new Error('Invalid Jitsi BOSH address.');
  }
  if (!websocket && !bosh) endpoint.searchParams.set('room', room);
  return { hosts, serviceUrl: endpoint.href };
}

function joinFailureMessage(reason) {
  const code = String(reason || '').toLowerCase();
  if (/password/.test(code)) return 'This Jitsi room requires a password. Open the meeting in your browser to enter it.';
  if (/lobby|members.?only|not.?allowed/.test(code)) return 'This Jitsi room requires admission or authorized membership. Open the meeting in your browser.';
  if (/auth|token|not.?authorized/.test(code)) return 'This Jitsi server requires authentication. Open the meeting in your browser to sign in.';
  return 'Could not join the Jitsi room. Check its address and the server access rules.';
}

export async function createConference(options, handlers) {
  const room = String(options.room || '').toLowerCase();
  if (!room) throw new Error('room is required');

  const resources = deploymentUrls(options.serverUrl);
  // A reconnect/server change must not reuse the previous deployment's config.
  window.config = undefined;
  await loadScript(resources.config);
  await loadScript(resources.library);
  const J = window.JitsiMeetJS;
  J.init({});
  J.setLogLevel(J.logLevels.WARN);

  const connection = new J.JitsiConnection(null, options.jwt || null,
    connectionOptions(options.serverUrl, room, window.config || {}));

  let joined = false;
  let closed = false;
  let localId = null;
  let conference = null;
  let joinTimer = null;
  let connectionTimer = null;
  const localTracks = { audio: null, video: null };

  // One failure path: before the conference is joined it is a join failure,
  // afterwards a lost connection.
  const fail = (reason) => {
    if (closed) return;
    if (joined) handlers.onConnectionLost(reason);
    else handlers.onFailed(reason);
    void cleanup().catch(() => {});
  };

  // Frees the camera/mic and the XMPP connection; used on every exit path.
  async function cleanup() {
    closed = true;
    clearTimeout(joinTimer);
    clearTimeout(connectionTimer);
    for (const track of Object.values(localTracks)) {
      if (track) await track.dispose().catch(() => {});
    }
    if (conference) await conference.leave().catch(() => {});
    await Promise.resolve(connection.disconnect()).catch(() => {});
  }

  // Failures while connecting reject (the caller reports a join failure);
  // later ones go through fail().
  let established = false;
  try {
    await new Promise((resolve, reject) => {
    connectionTimer = setTimeout(() => reject(new Error('The Jitsi server did not respond. Check the address or open the meeting in your browser.')), 30000);
    connection.addEventListener(J.events.connection.CONNECTION_ESTABLISHED, () => {
      clearTimeout(connectionTimer);
      established = true;
      resolve();
    });
    connection.addEventListener(J.events.connection.CONNECTION_FAILED, (e) => {
      clearTimeout(connectionTimer);
      const reason = joinFailureMessage(e);
      if (established) fail(reason);
      else reject(new Error(reason));
    });
    connection.addEventListener(J.events.connection.CONNECTION_DISCONNECTED, () => {
      clearTimeout(connectionTimer);
      if (established) fail('The connection to the Jitsi server was lost.');
      else reject(new Error('The Jitsi server disconnected before the meeting was reached.'));
    });
    connection.connect();
    });
  } catch (err) {
    await cleanup();
    throw err;
  }

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
  const announce = (id, user) =>
    handlers.onUserJoined({ participantId: id, displayName: (user && user.getDisplayName()) || id, isLocal: false });
  conference.on(J.events.conference.USER_JOINED, announce);
  conference.on(J.events.conference.USER_LEFT, (id) => handlers.onParticipantLeft(id));
  conference.on(J.events.conference.DISPLAY_NAME_CHANGED, (id, name) => handlers.onDisplayName(id, name));
  conference.on(J.events.conference.CONFERENCE_FAILED, (err) => fail(joinFailureMessage(err)));
  if (J.events.conference.PASSWORD_REQUIRED) {
    conference.on(J.events.conference.PASSWORD_REQUIRED, () => fail(joinFailureMessage('password')));
  }
  conference.on(J.events.conference.KICKED, () => fail('kicked'));
  conference.on(J.events.conference.CONFERENCE_JOINED, () => {
    if (closed) return;
    clearTimeout(joinTimer);
    joined = true;
    localId = conference.myUserId();
    handlers.onJoined(localId);
    handlers.onUserJoined({ participantId: localId, displayName: options.displayName || localId, isLocal: true });
    // Members that were already present (the app ignores duplicates).
    conference.getParticipants().forEach((p) => announce(p.getId(), p));
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

  try {
    if (options.startWithMicrophone !== false) await attach('audio');
    if (options.startWithCamera !== false) await attach('video');
    conference.join();
    if (!closed && !joined) {
      joinTimer = setTimeout(() => fail('The Jitsi room did not admit you. It may require a host, lobby approval or sign-in. Open the meeting in your browser.'), 30000);
    }
  } catch (err) {
    await cleanup();
    throw err;
  }

  async function setEnabled(kind, enabled) {
    const track = localTracks[kind];
    if (!track) {
      if (enabled) await attach(kind);
      return;
    }
    await (enabled ? track.unmute() : track.mute());
  }

  return {
    leave: cleanup,
    setMicrophoneEnabled: (enabled) => setEnabled('audio', enabled),
    setCameraEnabled: (enabled) => setEnabled('video', enabled),
  };
}
