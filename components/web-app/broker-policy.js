export const DEFAULT_MQTT_WEBSOCKET_PORTS = Object.freeze({
  "ws:": "1884",
  "wss:": "8884",
});

export function normalizeBrokerWebSocketUrl(input) {
  const raw = String(input ?? "").trim();
  const scheme = raw.match(/^wss?:\/\//i);
  if (!scheme) return raw;

  try {
    const parsed = new URL(raw);
    const authority = raw
      .slice(scheme[0].length)
      .split(/[/?#]/, 1)[0]
      .split("@").at(-1);
    const hasExplicitPort = authority.startsWith("[")
      ? /^\[[^\]]+\]:\d+$/.test(authority)
      : /:\d+$/.test(authority);
    if (hasExplicitPort) return raw;
    parsed.port = DEFAULT_MQTT_WEBSOCKET_PORTS[parsed.protocol];
    return parsed.toString();
  } catch {
    return raw;
  }
}

export function evaluateBrokerTransport(
  url,
  { native = false, pageProtocol = "http:" } = {},
) {
  if (!/^wss?:\/\//i.test(url)) {
    return {
      allowed: false,
      insecure: false,
      message: "Enter an MQTT WebSocket URL beginning with ws:// or wss://.",
    };
  }
  try {
    const parsed = new URL(url);
    if (!parsed.hostname) throw new Error("missing hostname");
  } catch {
    return {
      allowed: false,
      insecure: false,
      message: "Enter a valid MQTT WebSocket URL.",
    };
  }
  const insecure = url.toLowerCase().startsWith("ws://");
  if (!native && pageProtocol === "https:" && insecure) {
    return {
      allowed: false,
      insecure,
      message: "This HTTPS page requires a secure wss:// MQTT WebSocket endpoint.",
    };
  }
  return { allowed: true, insecure, message: "" };
}
