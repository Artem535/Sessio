import { afterEach, expect, test, vi } from 'vitest';
import { joinGuest } from '../src/join-api';
afterEach(() => vi.unstubAllGlobals());
test('exchanges web credentials only with same-origin API and validates WSS', async () => {
  const result = {endpointUrl:'wss://media.example.test',roomName:'room',token:'test-token',expiresAt:123};
  const fetch = vi.fn().mockResolvedValue(new Response(JSON.stringify(result)));
  vi.stubGlobal('fetch', fetch);
  expect(await joinGuest({code:'a/b',passcode:'p'},'Гость',new AbortController().signal)).toEqual(result);
  expect(fetch.mock.calls[0][0]).toBe('/v1/invitations/a%2Fb/client-token');
  expect(JSON.parse(fetch.mock.calls[0][1].body).clientKind).toBe('web');
  fetch.mockResolvedValue(new Response(JSON.stringify({...result,endpointUrl:'ws://unsafe'})));
  await expect(joinGuest({code:'c',passcode:'p'},'Гость',new AbortController().signal)).rejects.toThrow('invalid_response');
});
test('never displays arbitrary response text', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('secret diagnostic',{status:403})));
  await expect(joinGuest({code:'c',passcode:'p'},'Гость',new AbortController().signal)).rejects.toThrow('join_denied');
});
