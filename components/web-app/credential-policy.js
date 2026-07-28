// A device owner key is 256 random bits encoded as lowercase hexadecimal. It
// is machine-managed, never typed as a PIN/password, and authorizes the same
// application protocol over BLE and local HTTP.
export function generateOwnerKey(cryptoImpl = crypto) {
  if (!cryptoImpl?.getRandomValues) throw new Error("Secure random generation is unavailable");
  const bytes = new Uint8Array(32);
  cryptoImpl.getRandomValues(bytes);
  return [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
}

export function isOwnerKey(value) {
  return /^[0-9a-f]{64}$/.test(String(value || ""));
}

// Device state is authoritative. A locally staged key may survive an
// interrupted CLAIM or a factory reset, so an unclaimed device must always be
// claimed (using that same key) instead of attempting AUTH.
export function ownerKeyAction(info, ownerKey) {
  if (!info?.owner_key_configured) return "claim";
  return isOwnerKey(ownerKey) ? "auth" : "unavailable";
}
