import test from "node:test";
import assert from "node:assert/strict";

import { generateOwnerKey, isOwnerKey } from "../credential-policy.js";
import { createDeviceTransfer, parseDeviceTransfer } from "../device-sharing.js";

test("owner keys contain 256 random bits encoded as hexadecimal", () => {
  const bytes = Uint8Array.from({ length: 32 }, (_, index) => index);
  const ownerKey = generateOwnerKey({ getRandomValues: (target) => target.set(bytes) });
  assert.equal(ownerKey.length, 64);
  assert.equal(ownerKey, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  assert.equal(isOwnerKey(ownerKey), true);
});

test("device transfer QR payload round-trips owner authority", () => {
  const ownerKey = "ab".repeat(32);
  const payload = createDeviceTransfer(
    { id: "tracker-one", name: "Pasture", role: "tracker", address: "http://tracker.local" },
    ownerKey,
  );
  assert.deepEqual(parseDeviceTransfer(payload), {
    type: "lora-tracker-owner-key",
    version: 1,
    device: {
      id: "tracker-one",
      name: "Pasture",
      role: "tracker",
      address: "http://tracker.local",
    },
    owner_key: ownerKey,
  });
});

test("invalid or truncated owner-key transfers are rejected", () => {
  assert.throws(
    () => parseDeviceTransfer(JSON.stringify({ type: "lora-tracker-owner-key", version: 1, device: { id: "x", role: "tracker" }, owner_key: "abcd" })),
    /invalid/i,
  );
});
