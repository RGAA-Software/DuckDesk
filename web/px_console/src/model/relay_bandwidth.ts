import type { ManagedRelay } from "./managed_relay_api";

export type RelayBandwidthState = "unconfigured" | "unknown" | "available" | "busy";

/** Matches Console admission: preserve ten percent for transport overhead and bursts. */
export function relayBandwidthState(
    relay: Pick<ManagedRelay, "bandwidth_capacity_mbps" | "forwarded_rate_bps">,
): RelayBandwidthState {
    const capacity = relay.bandwidth_capacity_mbps;
    if (capacity === null || capacity === undefined) return "unconfigured";
    const rate = relay.forwarded_rate_bps;
    if (
        !Number.isSafeInteger(capacity) ||
        capacity < 1 ||
        capacity > 100_000 ||
        rate === null ||
        !Number.isSafeInteger(rate) ||
        rate < 0
    )
        return "unknown";
    return rate >= capacity * 900_000 ? "busy" : "available";
}
