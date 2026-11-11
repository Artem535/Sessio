import { describe, expect, it } from 'vitest';
import { ParticipantRegistry } from '../src/participants.js';

describe('ParticipantRegistry', () => {
  it('reports a participant as new only on the first join', () => {
    const r = new ParticipantRegistry();
    expect(r.join('a')).toBe(true);
    expect(r.join('a')).toBe(false);
    expect(r.join('b')).toBe(true);
    expect(r.has('a')).toBe(true);
  });

  it('leave reports presence once', () => {
    const r = new ParticipantRegistry();
    r.join('a');
    expect(r.leave('a')).toBe(true);
    expect(r.leave('a')).toBe(false);
    expect(r.has('a')).toBe(false);
  });

  it('remove-then-add track swap keeps the participant present and seq continuing', () => {
    const r = new ParticipantRegistry();
    const t1 = {};
    r.join('a');
    r.addTrack('a', t1);
    expect(r.seq('a')).toBe(0);
    expect(r.seq('a')).toBe(1);
    expect(r.removeTrack('a', t1)).toBe(0);
    expect(r.has('a')).toBe(true);
    r.addTrack('a', {});
    expect(r.has('a')).toBe(true);
    expect(r.seq('a')).toBe(2);
  });

  it('add-then-remove swap leaves the newer track live', () => {
    const r = new ParticipantRegistry();
    const old = {};
    r.join('a');
    r.addTrack('a', old);
    r.addTrack('a', {});
    expect(r.removeTrack('a', old)).toBe(1);
    expect(r.removeTrack('a', old)).toBe(1); // stale repeat
    expect(r.has('a')).toBe(true);
  });

  it('seq is per participant and survives leave/rejoin of the same id', () => {
    const r = new ParticipantRegistry();
    r.join('a');
    r.seq('a');
    r.join('b');
    expect(r.seq('b')).toBe(0);
    r.leave('a');
    r.join('a');
    expect(r.seq('a')).toBe(1);
  });

  it('ignores removal for unknown participants', () => {
    const r = new ParticipantRegistry();
    expect(r.removeTrack('zz', {})).toBe(0);
  });
});
