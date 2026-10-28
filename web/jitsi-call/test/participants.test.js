import { describe, expect, it } from 'vitest';
import { ParticipantRegistry } from '../src/participants.js';

describe('ParticipantRegistry', () => {
  it('flags only the first track as a new participant', () => {
    const r = new ParticipantRegistry();
    expect(r.addTrack('a', {}).isNewParticipant).toBe(true);
    expect(r.addTrack('a', {}).isNewParticipant).toBe(false);
    expect(r.addTrack('b', {}).isNewParticipant).toBe(true);
  });

  it('continues seq across track re-adds and is per participant', () => {
    const r = new ParticipantRegistry();
    const t1 = {};
    r.addTrack('a', t1);
    expect(r.seq('a')).toBe(0);
    expect(r.seq('a')).toBe(1);
    r.removeTrack('a', t1);
    r.addTrack('a', {});
    expect(r.seq('a')).toBe(2);
    r.addTrack('b', {});
    expect(r.seq('b')).toBe(0);
    expect(r.seq('b')).toBe(1);
  });

  it('reports participantGone only when the last track goes', () => {
    const r = new ParticipantRegistry();
    const t1 = {};
    const t2 = {};
    r.addTrack('a', t1);
    r.addTrack('a', t2);
    expect(r.removeTrack('a', t1).participantGone).toBe(false);
    expect(r.removeTrack('a', t2).participantGone).toBe(true);
  });

  it('ignores a stale removeTrack after a newer addTrack', () => {
    const r = new ParticipantRegistry();
    const old = {};
    r.addTrack('a', old);
    r.addTrack('a', {});
    r.removeTrack('a', old);
    expect(r.removeTrack('a', old).participantGone).toBe(false);
    expect(r.has('a')).toBe(true);
  });

  it('ignores removal of unknown participants', () => {
    const r = new ParticipantRegistry();
    expect(r.removeTrack('zz', {}).participantGone).toBe(false);
  });

  it('keeps seq monotonic even if the only track was swapped remove-then-add', () => {
    const r = new ParticipantRegistry();
    const t = {};
    r.addTrack('a', t);
    r.seq('a');
    expect(r.removeTrack('a', t).participantGone).toBe(true);
    r.addTrack('a', {});
    expect(r.seq('a')).toBe(1);
  });
});
