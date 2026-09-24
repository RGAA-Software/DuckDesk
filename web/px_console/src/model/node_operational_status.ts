import type { ManagedNode, NodeTelemetry } from "@/model/managed_node_api";

const TELEMETRY_FRESHNESS_MS = 30_000;
const CLOCK_SKEW_ALLOWANCE_MS = 5_000;

export type NodeOperationalStatus = "ready" | "draining" | "disabled" | "offline" | "notReady" | "unknown";

export function nodeOperationalStatus(
    node: Pick<ManagedNode, "disabled" | "draining" | "fresh" | "state">,
    snapshotCurrent = true,
): NodeOperationalStatus {
    if (node.disabled) return "disabled";
    if (!snapshotCurrent) return "unknown";
    if (!node.fresh) return "offline";
    if (node.state !== "ready") return "notReady";
    if (node.draining) return "draining";
    return "ready";
}

export function currentNodeTelemetry(
    node: Pick<ManagedNode, "fresh" | "telemetry">,
    observedAtMs: number,
    snapshotCurrent = true,
): NodeTelemetry | null {
    if (!snapshotCurrent || !node.fresh || !node.telemetry) return null;
    const receivedAtMs = Date.parse(node.telemetry.received_at);
    const ageMs = observedAtMs - receivedAtMs;
    if (!Number.isFinite(ageMs) || ageMs < -CLOCK_SKEW_ALLOWANCE_MS || ageMs > TELEMETRY_FRESHNESS_MS) {
        return null;
    }
    return node.telemetry;
}
