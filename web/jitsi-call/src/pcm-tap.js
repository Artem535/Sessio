// Collects 16 kHz mono float frames into fixed-size int16 chunks and posts
// each chunk's buffer (transferred, not copied) to the main thread.
class PcmTap extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.size = options.processorOptions.chunkSamples;
    this.buf = new Int16Array(this.size);
    this.n = 0;
  }

  process(inputs) {
    const channel = inputs[0] && inputs[0][0];
    if (!channel) return true;
    for (let i = 0; i < channel.length; i++) {
      const s = Math.max(-1, Math.min(1, channel[i]));
      this.buf[this.n++] = s < 0 ? s * 0x8000 : s * 0x7fff;
      if (this.n === this.size) {
        this.port.postMessage(this.buf.buffer, [this.buf.buffer]);
        this.buf = new Int16Array(this.size);
        this.n = 0;
      }
    }
    return true;
  }
}

registerProcessor('pcm-tap', PcmTap);
