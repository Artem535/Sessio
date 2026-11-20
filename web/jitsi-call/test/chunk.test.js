import { describe, expect, it } from 'vitest';
import { encodeChunk } from '../src/chunk.js';

describe('encodeChunk', () => {
  it('lays out the v1 header, id and PCM', () => {
    const pcm = new Int16Array([1, -2, 300]);
    const out = encodeChunk('Анна', 7, 1234.5, pcm.buffer);
    const id = new TextEncoder().encode('Анна');
    const dv = new DataView(out.buffer, out.byteOffset, out.byteLength);
    expect(out.length).toBe(16 + id.length + 6);
    expect(dv.getUint8(0)).toBe(1);
    expect(dv.getUint8(1)).toBe(id.length);
    expect(dv.getUint16(2, true)).toBe(0);
    expect(dv.getUint32(4, true)).toBe(7);
    expect(dv.getFloat64(8, true)).toBe(1234.5);
    expect(Array.from(out.subarray(16, 16 + id.length))).toEqual(Array.from(id));
    const body = new Int16Array(out.slice(16 + id.length).buffer);
    expect(Array.from(body)).toEqual([1, -2, 300]);
  });

  it('rejects ids longer than 255 bytes', () => {
    expect(() => encodeChunk('x'.repeat(256), 0, 0, new ArrayBuffer(2))).toThrow();
  });
});
