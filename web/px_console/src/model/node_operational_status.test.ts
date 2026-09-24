import { describe, expect, it } from "vitest";
import { currentNodeTelemetry, nodeOperationalStatus } from "./node_operational_status";
import type { ManagedNode, NodeTelemetry } from "./managed_node_api";

const telemetry: NodeTelemetry = {
    node_id: "00000000-0000-0000-0000-000000000001",
    node_generation: 1,
    report_sequence: 1,
    probe_state: "ready",
    sampled_at: "2026-09-24T12:00:00Z",
    received_at: "2026-09-24T12:00:00Z",
    logical_processors: 16,
    cpu_utilization_per_mille: 300,
    memory_total_bytes: 64,
    memory_available_bytes: 32,
    disk_total_bytes: 128,
    disk_free_bytes: 64,
    gpu_inventory_revision: 1,
};

describe("node operational status", () => {
    it("does not show a disabled, stale, unreconciled, or draining node as ready", () => {
        expect(nodeOperationalStatus({ disabled: true, fresh: true, state: "ready", draining: false })).toBe("disabled");
        expect(nodeOperationalStatus({ disabled: false, fresh: false, state: "ready", draining: false })).toBe("offline");
        expect(nodeOperationalStatus({ disabled: false, fresh: true, state: "reconciling", draining: false })).toBe("notReady");
        expect(nodeOperationalStatus({ disabled: false, fresh: true, state: "ready", draining: true })).toBe("draining");
        expect(nodeOperationalStatus({ disabled: false, fresh: true, state: "ready", draining: false })).toBe("ready");
        expect(nodeOperationalStatus({ disabled: false, fresh: true, state: "ready", draining: false }, false)).toBe("unknown");
    });

    it("hides old or invalid telemetry without turning unknown measurements into zero", () => {
        const observedAtMs = Date.parse("2026-09-24T12:00:20Z");
        const node = { fresh: true, telemetry } satisfies Pick<ManagedNode, "fresh" | "telemetry">;
        expect(currentNodeTelemetry(node, observedAtMs)).toBe(telemetry);
        expect(currentNodeTelemetry(node, observedAtMs + 11_000)).toBeNull();
        expect(currentNodeTelemetry({ ...node, fresh: false }, observedAtMs)).toBeNull();
        expect(currentNodeTelemetry(node, observedAtMs, false)).toBeNull();
        expect(currentNodeTelemetry({ ...node, telemetry: null }, observedAtMs)).toBeNull();
        expect(currentNodeTelemetry({ ...node, telemetry: { ...telemetry, received_at: "invalid" } }, observedAtMs)).toBeNull();
    });
});
