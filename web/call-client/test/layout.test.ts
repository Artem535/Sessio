// @vitest-environment jsdom
import { expect, test, vi } from 'vitest';
vi.mock('livekit-client', () => ({ RoomEvent:{}, Track:{Source:{Camera:'camera',Microphone:'microphone',ScreenShare:'screen_share'}} }));
import { gridColumns } from '../src/CallRoom';
import { initials } from '../src/ParticipantTile';

test('grid stays as square as the room allows on a wide window', () => {
  expect([1,2,3,4,5,9,10].map(n=>gridColumns(n,1280))).toEqual([1,2,2,2,3,3,4]);
});

test('a phone held upright stacks tiles in one column until it gets crowded', () => {
  expect([1,2,4,5].map(n=>gridColumns(n,375))).toEqual([1,1,1,2]);
});

test('avatar initials use the first two words or the first two letters', () => {
  expect(initials('Мария Иванова')).toBe('МИ');
  expect(initials('artem')).toBe('AR');
  expect(initials('   ')).toBe('?');
});
