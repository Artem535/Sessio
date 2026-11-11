// Wire format v1 shared with the C++ audio bridge (little endian):
// u8 version=1 | u8 idLen | u16 reserved | u32 seq | f64 sentMs | id (UTF-8) | int16 mono PCM
const encoder = new TextEncoder();

export function encodeChunk(participantId, seq, sentMs, int16Buffer) {
  const id = encoder.encode(participantId);
  // idLen is a single byte; a longer id would silently wrap and corrupt the frame.
  if (id.length > 255) throw new Error('participant id too long');
  const pcm = new Uint8Array(int16Buffer);
  const out = new Uint8Array(16 + id.length + pcm.length);
  const dv = new DataView(out.buffer);
  dv.setUint8(0, 1);
  dv.setUint8(1, id.length);
  dv.setUint32(4, seq >>> 0, true);
  dv.setFloat64(8, sentMs, true);
  out.set(id, 16);
  out.set(pcm, 16 + id.length);
  return out;
}
