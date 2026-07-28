import QRCode from "qrcode";
import QrScanner from "qr-scanner";
import { isOwnerKey } from "./credential-policy.js";

export const DEVICE_TRANSFER_TYPE = "lora-tracker-owner-key";
export const DEVICE_TRANSFER_VERSION = 1;

export function createDeviceTransfer(record, ownerKey) {
  if (!record?.id || !["tracker", "gateway", "repeater"].includes(record.role)) {
    throw new Error("Select a saved device before exporting access");
  }
  if (!isOwnerKey(ownerKey)) throw new Error("The selected device has no valid owner key");
  return JSON.stringify({
    type: DEVICE_TRANSFER_TYPE,
    version: DEVICE_TRANSFER_VERSION,
    device: {
      id: String(record.id),
      name: String(record.name || record.id),
      role: record.role,
      address: record.address || null,
    },
    owner_key: ownerKey,
  });
}

export function parseDeviceTransfer(text) {
  let value;
  try {
    value = JSON.parse(String(text));
  } catch {
    throw new Error("This QR code is not a LoRa Tracker device transfer");
  }
  if (
    value?.type !== DEVICE_TRANSFER_TYPE ||
    value?.version !== DEVICE_TRANSFER_VERSION ||
    !value.device?.id ||
    !["tracker", "gateway", "repeater"].includes(value.device.role) ||
    !isOwnerKey(value.owner_key)
  ) {
    throw new Error("Unsupported or invalid device transfer QR code");
  }
  return value;
}

export function renderDeviceTransferQr(text) {
  return QRCode.toDataURL(text, {
    errorCorrectionLevel: "Q",
    margin: 2,
    width: 720,
    color: { dark: "#07120b", light: "#ffffff" },
  });
}

export async function scanDeviceTransferImage(file) {
  const result = await QrScanner.scanImage(file, { returnDetailedScanResult: true });
  return parseDeviceTransfer(result.data);
}
