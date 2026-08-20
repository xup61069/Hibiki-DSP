// SPDX-License-Identifier: GPL-3.0-only

// Browser-side packetizer. It never captures a tab by itself: offscreen.js
// installs this node only after the popup click has obtained a tab stream.
class HibikiTabPacketizer extends AudioWorkletProcessor {
  process(inputs, outputs) {
    const input = inputs[0] ?? [];
    const output = outputs[0] ?? [];
    const channels = Math.min(input.length, 8);
    const frames = channels > 0 ? input[0].length : 0;
    for (let channel = 0; channel < output.length; channel += 1) {
      const source = input[channel];
      const destination = output[channel];
      for (let frame = 0; frame < destination.length; frame += 1) {
        destination[frame] = source?.[frame] ?? 0;
      }
    }
    if (channels === 0 || frames === 0) return true;

    // HIBT + version + channels + frames + sample rate, little-endian.
    const packet = new ArrayBuffer(16 + channels * frames * 4);
    const view = new DataView(packet);
    view.setUint8(0, 0x48); // H
    view.setUint8(1, 0x49); // I
    view.setUint8(2, 0x42); // B
    view.setUint8(3, 0x54); // T
    view.setUint16(4, 1, true);
    view.setUint16(6, channels, true);
    view.setUint32(8, frames, true);
    view.setUint32(12, sampleRate, true);
    const interleaved = new Float32Array(packet, 16);
    for (let frame = 0; frame < frames; frame += 1) {
      for (let channel = 0; channel < channels; channel += 1) {
        interleaved[frame * channels + channel] = input[channel][frame];
      }
    }
    this.port.postMessage(packet, [packet]);
    return true;
  }
}

registerProcessor('hibiki-tab-packetizer', HibikiTabPacketizer);
