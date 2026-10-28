// Tracks which media tracks belong to which participant. Jitsi swaps a
// participant's tracks when it switches between P2P and the bridge, so audio
// state (seq, tap) must live on the participant, never on the track.
export class ParticipantRegistry {
  constructor() {
    this.entries = new Map(); // id -> Set of live tracks
    // Kept even after the participant is gone: a remove-then-add swap of the
    // only track must not make the receiver see seq jump back to 0.
    this.seqs = new Map();
  }

  has(participantId) {
    return this.entries.has(participantId);
  }

  addTrack(participantId, track) {
    let tracks = this.entries.get(participantId);
    const isNewParticipant = !tracks;
    if (!tracks) {
      tracks = new Set();
      this.entries.set(participantId, tracks);
    }
    tracks.add(track);
    return { isNewParticipant };
  }

  // A removal of a track that was already replaced (or never added) must not
  // report the participant as gone while a newer track is live.
  removeTrack(participantId, track) {
    const tracks = this.entries.get(participantId);
    if (!tracks) return { participantGone: false };
    tracks.delete(track);
    if (tracks.size > 0) return { participantGone: false };
    this.entries.delete(participantId);
    return { participantGone: true };
  }

  // Forced removal for an explicit "user left" signal; true if it was known.
  drop(participantId) {
    return this.entries.delete(participantId);
  }

  seq(participantId) {
    const n = this.seqs.get(participantId) || 0;
    this.seqs.set(participantId, n + 1);
    return n;
  }
}
