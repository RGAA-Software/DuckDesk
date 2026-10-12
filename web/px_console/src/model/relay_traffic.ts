import type { ManagedRelay } from "./managed_relay_api";
import { managementSnapshotCurrent } from "./management_snapshot";

type RelayTrafficProfile = Pick<ManagedRelay,
    "id" | "generation" | "control_epoch" | "report_sequence" | "last_seen" |
    "uploaded_bytes" | "forwarded_bytes" | "fresh" | "state"
>;

export interface RelayTrafficRate {
    receivedMbps: number;
    forwardedMbps: number;
    observedAtMs: number;
}

interface RelayTrafficObservation {
    generation: number;
    controlEpoch: number;
    reportSequence: number;
    reportedAtMs: number;
    receivedBytes: number;
    forwardedBytes: number;
    observedAtMs: number;
    rate: RelayTrafficRate | undefined;
}

function counterValid(counter: number | null): counter is number {
    return counter !== null && Number.isSafeInteger(counter) && counter >= 0;
}

function observationFor(relay: RelayTrafficProfile, observedAtMs: number): RelayTrafficObservation | undefined {
    const reportedAtMs = relay.last_seen === null ? NaN : Date.parse(relay.last_seen);
    if (!relay.fresh || relay.state !== "ready" || !Number.isFinite(reportedAtMs) ||
        !counterValid(relay.generation) || relay.generation === 0 ||
        !counterValid(relay.control_epoch) || relay.control_epoch === 0 ||
        !counterValid(relay.report_sequence) || relay.report_sequence === 0 ||
        !counterValid(relay.uploaded_bytes) || !counterValid(relay.forwarded_bytes)) {
        return undefined;
    }
    return {
        generation: relay.generation,
        controlEpoch: relay.control_epoch,
        reportSequence: relay.report_sequence,
        reportedAtMs,
        receivedBytes: relay.uploaded_bytes,
        forwardedBytes: relay.forwarded_bytes,
        observedAtMs,
        rate: undefined,
    };
}

/** Interval averages from authenticated reports, never a configured bandwidth capacity. */
export class RelayTrafficTracker {
    private readonly observations = new Map<string, RelayTrafficObservation>();

    observe(relays: readonly RelayTrafficProfile[], observedAtMs: number): ReadonlyMap<string, RelayTrafficRate> {
        const rates = new Map<string, RelayTrafficRate>();
        const retainedIds = new Set(relays.map(relay => relay.id));
        for (const relayId of this.observations.keys()) {
            if (!retainedIds.has(relayId)) this.observations.delete(relayId);
        }
        for (const relay of relays) {
            const current = Number.isFinite(observedAtMs) && observedAtMs >= 0
                ? observationFor(relay, observedAtMs) : undefined;
            if (!current) {
                this.observations.delete(relay.id);
                continue;
            }
            const previous = this.observations.get(relay.id);
            const sameRuntime = previous?.generation === current.generation &&
                previous.controlEpoch === current.controlEpoch;
            if (previous && sameRuntime && current.reportSequence <= previous.reportSequence) {
                // Repeated inventory reads cannot extend the lifetime of an old rate.
                if (previous.rate && managementSnapshotCurrent(previous.rate.observedAtMs, observedAtMs)) {
                    rates.set(relay.id, previous.rate);
                }
                continue;
            }
            const elapsedMs = previous ? current.reportedAtMs - previous.reportedAtMs : 0;
            if (previous && sameRuntime && elapsedMs > 0 && elapsedMs <= 30_000 &&
                managementSnapshotCurrent(previous.observedAtMs, observedAtMs) &&
                current.receivedBytes >= previous.receivedBytes && current.forwardedBytes >= previous.forwardedBytes) {
                current.rate = {
                    receivedMbps: (current.receivedBytes - previous.receivedBytes) * 8 / (elapsedMs * 1_000),
                    forwardedMbps: (current.forwardedBytes - previous.forwardedBytes) * 8 / (elapsedMs * 1_000),
                    observedAtMs,
                };
                rates.set(relay.id, current.rate);
            }
            this.observations.set(relay.id, current);
        }
        return rates;
    }
}
