import { afterEach, describe, expect, it, vi } from 'vitest';
import { createConference, deploymentUrls, connectionOptions } from '../src/jitsi-adapter.js';

afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers(); });

describe('Jitsi deployment configuration', () => {
  it('loads resources below the deployment prefix with or without its final slash', () => {
    for (const base of ['https://meet.test/meet', 'https://meet.test/meet/']) {
      expect(deploymentUrls(base)).toEqual({
        config: 'https://meet.test/meet/config.js',
        library: 'https://meet.test/meet/libs/lib-jitsi-meet.min.js',
      });
    }
  });
  it('respects server XMPP hosts and an absolute websocket including its query', () => {
    const hosts = { domain: 'xmpp.test', muc: 'rooms.xmpp.test' };
    expect(connectionOptions('https://meet.test/meet', 'a b', {
      hosts, websocket: 'wss://xmpp.test/custom?tenant=x', bosh: 'https://ignored.test/bosh',
    })).toEqual({ hosts, serviceUrl: 'wss://xmpp.test/custom?tenant=x' });
  });
  it('resolves relative websocket and bosh against the deployment base', () => {
    expect(connectionOptions('https://meet.test/meet', 'room', { websocket: 'xmpp-websocket' }).serviceUrl)
      .toBe('wss://meet.test/meet/xmpp-websocket');
    expect(connectionOptions('https://meet.test/meet/', 'room', { bosh: 'http-bind?tenant=x' }).serviceUrl)
      .toBe('https://meet.test/meet/http-bind?tenant=x');
  });
  it('uses prefix and escaped room in the fallback and leaves ports out of XMPP domain', () => {
    expect(connectionOptions('http://localhost:8080/meet', 'a b', {})).toEqual({
      hosts: { domain: 'localhost', muc: 'conference.localhost' },
      serviceUrl: 'ws://localhost:8080/meet/xmpp-websocket?room=a+b',
    });
  });
  it('rejects credentials and unsafe endpoint protocols from config', () => {
    expect(() => connectionOptions('https://meet.test', 'r', { websocket: 'javascript:alert(1)' })).toThrow();
    expect(() => connectionOptions('https://meet.test', 'r', { bosh: 'https://user:pass@meet.test/http-bind' })).toThrow();
  });
});

describe('Jitsi guest join', () => {
  function fakeSdk(failure) {
    const conferenceHandlers = new Map();
    const conference = {
      on: (event, fn) => conferenceHandlers.set(event, fn),
      setDisplayName: vi.fn(),
      join: vi.fn(() => failure && conferenceHandlers.get(failure.event || failure)?.(failure.reason)),
      leave: vi.fn(async () => {}),
    };
    let connection;
    const JitsiConnection = vi.fn(function (...args) {
      const listeners = new Map();
      connection = {
        args,
        addEventListener: (event, fn) => listeners.set(event, fn),
        connect: () => listeners.get('connected')(),
        disconnect: vi.fn(async () => {}),
        initJitsiConference: vi.fn(() => conference),
      };
      return connection;
    });
    vi.stubGlobal('window', { JitsiMeetJS: {
      init: vi.fn(), setLogLevel: vi.fn(), logLevels: { WARN: 1 }, JitsiConnection,
      events: {
        connection: { CONNECTION_ESTABLISHED: 'connected', CONNECTION_FAILED: 'failed', CONNECTION_DISCONNECTED: 'disconnected' },
        conference: { PASSWORD_REQUIRED: 'password', AUTH_STATUS_CHANGED: 'auth', LOBBY_USER_JOINED: 'lobby',
          TRACK_ADDED: 'track', TRACK_REMOVED: 'removed', USER_JOINED: 'user', USER_LEFT: 'left',
          DISPLAY_NAME_CHANGED: 'name', CONFERENCE_FAILED: 'failure', KICKED: 'kicked', CONFERENCE_JOINED: 'joined' },
      },
    } });
    vi.stubGlobal('document', {
      createElement: () => ({}),
      head: { appendChild: script => {
        if (script.src.endsWith('config.js')) window.config = { hosts: { domain: 'xmpp.test' }, bosh: 'http-bind' };
        script.onload();
      } },
    });
    return { conference, JitsiConnection, getConnection: () => connection };
  }
  it('uses no token for a direct guest join and consumes the deployment configuration', async () => {
    const sdk = fakeSdk();
    const call = await createConference({ serverUrl: 'https://meet.test/prefix', room: 'ROOM',
      startWithMicrophone: false, startWithCamera: false }, {});
    expect(sdk.JitsiConnection).toHaveBeenCalledWith(null, null, {
      hosts: { domain: 'xmpp.test' }, serviceUrl: 'https://meet.test/prefix/http-bind',
    });
    expect(sdk.getConnection().initJitsiConference).toHaveBeenCalledWith('room', {});
    await call.leave();
  });
  it('ends password-required joins with an actionable error rather than waiting forever', async () => {
    const sdk = fakeSdk('password');
    const onFailed = vi.fn();
    await createConference({ serverUrl: 'https://meet.test', room: 'r',
      startWithMicrophone: false, startWithCamera: false }, { onFailed });
    expect(onFailed).toHaveBeenCalledWith(expect.stringMatching(/password.*browser/i));
    await vi.waitFor(() => expect(sdk.getConnection().disconnect).toHaveBeenCalled());
  });
  it('explains server authentication without exposing the raw error details', async () => {
    fakeSdk({ event: 'failure', reason: 'conference.authenticationRequired https://private.test/?token=secret' });
    const onFailed = vi.fn();
    await createConference({ serverUrl: 'https://meet.test', room: 'r',
      startWithMicrophone: false, startWithCamera: false }, { onFailed });
    expect(onFailed).toHaveBeenCalledWith(expect.stringMatching(/authentication.*browser/i));
    expect(onFailed.mock.calls[0][0]).not.toContain('secret');
  });
  it('bounds a pending admission and cleans up its connection', async () => {
    vi.useFakeTimers();
    const sdk = fakeSdk();
    const onFailed = vi.fn();
    await createConference({ serverUrl: 'https://meet.test', room: 'r',
      startWithMicrophone: false, startWithCamera: false }, { onFailed });
    await vi.advanceTimersByTimeAsync(30000);
    expect(onFailed).toHaveBeenCalledWith(expect.stringMatching(/lobby approval.*browser/i));
    expect(sdk.getConnection().disconnect).toHaveBeenCalledOnce();
  });
});
