// Presence comes from conference membership (join/leave), never from track
// counts: a P2P<->JVB switch removes a participant's track and adds a new one
// while the participant stays in the call. Tracks and seq are bookkeeping on
// the participant, so audio state survives such swaps.
export class ParticipantRegistry {
  constructor() {
    this.present = new Set();
    this.tracks = new Map(); // id -> Set of live tracks
    // Kept after the participant leaves so a seq never jumps back for an id.
    this.seqs = new Map();
  }

  has(participantId) {
    return this.present.has(participantId);
  }

  // true if the participant was not yet present.
  join(participantId) {
    if (this.present.has(participantId)) return false;
    this.present.add(participantId);
    return true;
  }

  // true if the participant was present.
  leave(participantId) {
    this.tracks.delete(participantId);
    return this.present.delete(participantId);
  }

  addTrack(participantId, track) {
    let tracks = this.tracks.get(participantId);
    if (!tracks) {
      tracks = new Set();
      this.tracks.set(participantId, tracks);
    }
    tracks.add(track);
  }

  // Number of tracks still live; removing an unknown or already replaced
  // track is harmless.
  removeTrack(participantId, track) {
    const tracks = this.tracks.get(participantId);
    if (!tracks) return 0;
    tracks.delete(track);
    return tracks.size;
  }

  trackCount(participantId) {
    const tracks = this.tracks.get(participantId);
    return tracks ? tracks.size : 0;
  }

  seq(participantId) {
    const n = this.seqs.get(participantId) || 0;
    this.seqs.set(participantId, n + 1);
    return n;
  }
}
