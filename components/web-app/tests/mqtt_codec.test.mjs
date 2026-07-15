import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeRemainingLength, decodeRemainingLength, encodeConnect, encodePublish, parsePackets, decodePublish } from '../mqtt.js';

test('remaining length round trip', () => {
  for (const value of [0, 1, 127, 128, 16383, 16384, 268435455]) {
    const encoded = encodeRemainingLength(value);
    const wrapped = new Uint8Array([0, ...encoded]);
    const decoded = decodeRemainingLength(wrapped, 1);
    assert.equal(decoded.value, value);
    assert.equal(decoded.bytesUsed, encoded.length);
  }
});

test('connect packet uses MQTT 3.1.1', () => {
  const packet = encodeConnect({ clientId: 'test', username: 'u', password: 'p' });
  assert.equal(packet[0], 0x10);
  assert.ok(packet.includes(0x04));
});

test('publish packet round trip', () => {
  const encoded = encodePublish('equine/v1/test', '{"ok":true}');
  const parsed = parsePackets(encoded);
  assert.equal(parsed.packets.length, 1);
  const publish = decodePublish(parsed.packets[0]);
  assert.equal(publish.topic, 'equine/v1/test');
  assert.equal(publish.payload, '{"ok":true}');
});
