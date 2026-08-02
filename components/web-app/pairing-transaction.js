function requireCanonicalId(value, label) {
  const id = String(value || "");
  if (!/^[a-z0-9][a-z0-9-]{0,31}$/.test(id)) {
    throw new Error(`${label} has an invalid device ID`);
  }
  return id;
}

export function trackerRegistrationPatch(gatewayConfig, trackerConfig) {
  const trackers = Array.isArray(gatewayConfig?.trackers)
    ? gatewayConfig.trackers
    : [];
  const existing = trackers.findIndex(
    (tracker) => tracker.id === trackerConfig.device_id,
  );
  const reusable = trackers.findIndex((tracker) => tracker.enabled === false);
  const slot = existing >= 0 ? existing : reusable >= 0 ? reusable : trackers.length;
  if (slot >= 12) throw new Error("Gateway tracker registry is full");
  return {
    tracker_count: String(Math.max(trackers.length, slot + 1)),
    [`tracker.${slot}.id`]: trackerConfig.device_id,
    [`tracker.${slot}.name`]: trackerConfig.device_name,
    [`tracker.${slot}.lora_aead_key`]: trackerConfig.lora_aead_key,
    [`tracker.${slot}.enabled`]: "1",
  };
}

/**
 * Register first, confirm second. Both device operations are idempotent, so a
 * retry safely resumes if connectivity is lost between the two commits.
 */
export async function pairTrackerTransaction({
  trackerConfig,
  gatewayRecord,
  openGateway,
  confirmTracker,
  onStep = () => {},
}) {
  const trackerId = requireCanonicalId(trackerConfig?.device_id, "Tracker");
  const gatewayId = requireCanonicalId(gatewayRecord?.id, "Gateway");
  if (!/^[0-9a-f]{64}$/i.test(String(trackerConfig?.lora_aead_key || ""))) {
    throw new Error("Tracker has no valid LoRa encryption key");
  }

  onStep("locate", `Finding and authenticating ${gatewayId}`);
  const gateway = await openGateway(gatewayRecord);
  let registration;
  try {
    onStep("gateway", `Registering ${trackerId} on ${gatewayId}`);
    registration = await gateway.registerTracker(trackerConfig);
  } finally {
    await gateway.close?.();
  }
  if (registration?.tracker_id && registration.tracker_id !== trackerId) {
    throw new Error("Gateway confirmed a different tracker ID");
  }

  onStep("tracker", `Confirming ${gatewayId} on ${trackerId}`);
  let confirmation;
  try {
    confirmation = await confirmTracker(gatewayId);
  } catch (cause) {
    const error = new Error(
      `Gateway registration was saved, but tracker confirmation failed: ${cause.message}. Retry to finish`,
      { cause },
    );
    error.gatewayRegistrationCommitted = true;
    throw error;
  }
  if (!confirmation?.gateway_paired || confirmation.gateway_id !== gatewayId) {
    throw new Error("Tracker did not confirm the selected gateway");
  }

  onStep("complete", `${trackerId} and ${gatewayId} are paired`);
  return { registration, confirmation, trackerId, gatewayId };
}
