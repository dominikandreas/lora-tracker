const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();
const MAX_BUFFER_BYTES = 2 * 1024 * 1024;

function concatBytes(...arrays) {
  const size = arrays.reduce((sum, a) => sum + a.length, 0);
  const out = new Uint8Array(size);
  let offset = 0;
  for (const a of arrays) {
    out.set(a, offset);
    offset += a.length;
  }
  return out;
}

function utf8Field(value) {
  const encoded = textEncoder.encode(value ?? "");
  if (encoded.length > 65535) throw new Error("MQTT string too long");
  return concatBytes(
    new Uint8Array([encoded.length >> 8, encoded.length & 0xff]),
    encoded,
  );
}

export function encodeRemainingLength(length) {
  if (!Number.isInteger(length) || length < 0 || length > 268435455)
    throw new Error("Invalid MQTT remaining length");
  const bytes = [];
  do {
    let digit = length % 128;
    length = Math.floor(length / 128);
    if (length > 0) digit |= 0x80;
    bytes.push(digit);
  } while (length > 0);
  return new Uint8Array(bytes);
}

export function decodeRemainingLength(bytes, start = 1) {
  let multiplier = 1,
    value = 0,
    offset = start;
  for (let i = 0; i < 4; i++) {
    if (offset >= bytes.length) return null;
    const digit = bytes[offset++];
    value += (digit & 0x7f) * multiplier;
    if ((digit & 0x80) === 0) return { value, bytesUsed: offset - start };
    multiplier *= 128;
  }
  throw new Error("Malformed MQTT remaining length");
}

function packet(typeAndFlags, body) {
  return concatBytes(
    new Uint8Array([typeAndFlags]),
    encodeRemainingLength(body.length),
    body,
  );
}

export function encodeConnect({
  clientId,
  username = "",
  password = "",
  keepAlive = 30,
}) {
  let flags = 0x02; // clean session
  const payload = [utf8Field(clientId)];
  if (username) {
    flags |= 0x80;
    payload.push(utf8Field(username));
  }
  if (password) {
    flags |= 0x40;
    payload.push(utf8Field(password));
  }
  const variable = concatBytes(
    utf8Field("MQTT"),
    new Uint8Array([0x04, flags, keepAlive >> 8, keepAlive & 0xff]),
  );
  return packet(0x10, concatBytes(variable, ...payload));
}

export function encodeSubscribe(packetId, topics) {
  const body = [new Uint8Array([packetId >> 8, packetId & 0xff])];
  for (const topic of topics) body.push(utf8Field(topic), new Uint8Array([0]));
  return packet(0x82, concatBytes(...body));
}

export function encodePublish(topic, payload, retain = false) {
  const data =
    typeof payload === "string" ? textEncoder.encode(payload) : payload;
  return packet(0x30 | (retain ? 1 : 0), concatBytes(utf8Field(topic), data));
}

export function parsePackets(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const packets = [];
  let offset = 0;
  while (offset < bytes.length) {
    if (bytes.length - offset < 2) break;
    const remaining = decodeRemainingLength(bytes, offset + 1);
    if (!remaining) break;
    const headerLength = 1 + remaining.bytesUsed;
    const end = offset + headerLength + remaining.value;
    if (end > bytes.length) break;
    const first = bytes[offset];
    const type = first >> 4;
    const flags = first & 0x0f;
    const body = bytes.slice(offset + headerLength, end);
    packets.push({ type, flags, body });
    offset = end;
  }
  return { packets, remainder: bytes.slice(offset) };
}

export function decodePublish(packetData) {
  const body = packetData.body;
  if (body.length < 2) throw new Error("Malformed MQTT PUBLISH");
  const topicLength = (body[0] << 8) | body[1];
  if (2 + topicLength > body.length) throw new Error("Malformed MQTT topic");
  const topic = textDecoder.decode(body.slice(2, 2 + topicLength));
  const qos = (packetData.flags >> 1) & 0x03;
  let offset = 2 + topicLength;
  if (qos > 0) offset += 2;
  if (offset > body.length) throw new Error("Malformed MQTT packet id");
  return {
    topic,
    payload: textDecoder.decode(body.slice(offset)),
    retain: Boolean(packetData.flags & 1),
  };
}

export class MqttWebSocketClient extends EventTarget {
  constructor() {
    super();
    this.socket = null;
    this.packetId = 1;
    this.keepAliveTimer = null;
    this.reconnectTimer = null;
    this.reconnectAttempt = 0;
    this.manualClose = false;
    this.buffer = new Uint8Array();
    this.options = null;
    this.subscriptions = new Set();
  }

  connect(options) {
    this.options = { keepAlive: 30, ...options };
    this.manualClose = false;
    clearTimeout(this.reconnectTimer);
    this.#open();
  }

  #emit(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  #open() {
    if (!this.options) return;
    this.buffer = new Uint8Array();
    this.#emit("status", { state: "connecting" });
    let socket;
    try {
      socket = new WebSocket(this.options.url, ["mqtt"]);
    } catch (error) {
      this.#emit("error", { message: `Invalid WebSocket URL: ${error.message}` });
      this.#emit("status", { state: "offline" });
      return;
    }
    socket.binaryType = "arraybuffer";
    this.socket = socket;
    socket.onopen = () => {
      socket.send(
        encodeConnect({
          clientId: this.options.clientId,
          username: this.options.username,
          password: this.options.password,
          keepAlive: this.options.keepAlive,
        }),
      );
    };
    socket.onmessage = (event) => this.#onBytes(new Uint8Array(event.data));
    socket.onerror = () =>
      this.#emit("error", { message: "WebSocket transport error" });
    socket.onclose = () => {
      this.buffer = new Uint8Array();
      clearInterval(this.keepAliveTimer);
      this.keepAliveTimer = null;
      this.#emit("status", { state: "offline" });
      if (!this.manualClose) this.#scheduleReconnect();
    };
  }

  #onBytes(bytes) {
    if (this.buffer.length + bytes.length > MAX_BUFFER_BYTES) {
      this.#emit("error", { message: "MQTT packet exceeds the browser limit" });
      this.socket?.close();
      return;
    }
    const combined = concatBytes(this.buffer, bytes);
    const parsed = parsePackets(combined);
    this.buffer = parsed.remainder;
    for (const packetData of parsed.packets) {
      if (packetData.type === 2) {
        if (packetData.body.length < 2 || packetData.body[1] !== 0) {
          const code = packetData.body[1] ?? -1;
          this.#emit("error", {
            message: `MQTT connection rejected (${code})`,
          });
          this.manualClose = true;
          this.socket?.close();
          continue;
        }
        this.reconnectAttempt = 0;
        this.#emit("status", { state: "online" });
        this.#startKeepAlive();
        if (this.subscriptions.size)
          this.#sendSubscribe([...this.subscriptions]);
      } else if (packetData.type === 3) {
        try {
          this.#emit("message", decodePublish(packetData));
        } catch (error) {
          this.#emit("error", { message: error.message });
        }
      } else if (packetData.type === 9) {
        this.#emit("subscribed", {});
      }
    }
  }

  #startKeepAlive() {
    clearInterval(this.keepAliveTimer);
    this.keepAliveTimer = setInterval(
      () => {
        if (this.socket?.readyState === WebSocket.OPEN)
          this.socket.send(new Uint8Array([0xc0, 0x00]));
      },
      Math.max(5000, this.options.keepAlive * 500),
    );
  }

  #scheduleReconnect() {
    const delay = Math.min(
      30000,
      1000 * 2 ** Math.min(this.reconnectAttempt++, 5),
    );
    this.#emit("status", { state: "reconnecting", delay });
    this.reconnectTimer = setTimeout(() => this.#open(), delay);
  }

  subscribe(...topics) {
    const added = topics
      .flat()
      .filter(Boolean)
      .filter((topic) => {
        if (this.subscriptions.has(topic)) return false;
        this.subscriptions.add(topic);
        return true;
      });
    if (this.socket?.readyState === WebSocket.OPEN && added.length)
      this.#sendSubscribe(added);
  }

  #sendSubscribe(topics) {
    if (!topics.length) return;
    this.packetId = this.packetId >= 65535 ? 1 : this.packetId + 1;
    this.socket.send(encodeSubscribe(this.packetId, topics));
  }

  publish(topic, payload, retain = false) {
    if (this.socket?.readyState !== WebSocket.OPEN)
      throw new Error("MQTT is not connected");
    this.socket.send(encodePublish(topic, payload, retain));
  }

  disconnect() {
    this.manualClose = true;
    this.buffer = new Uint8Array();
    clearTimeout(this.reconnectTimer);
    clearInterval(this.keepAliveTimer);
    if (this.socket?.readyState === WebSocket.OPEN)
      this.socket.send(new Uint8Array([0xe0, 0x00]));
    this.socket?.close();
    this.socket = null;
  }
}
