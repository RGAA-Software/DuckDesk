import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import { listManagedNodeTelemetry, type NodeTelemetryHistory } from "./managed_node_api";

vi.mock("@/http", () => ({
    default: {
        get: vi.fn(),
    },
}));

function telemetrySample(sequence: number): NodeTelemetryHistory {
    return {
        node_id: "00000000-0000-0000-0000-000000000001",
        node_generation: 3,
        report_sequence: sequence,
        probe_state: "ready",
        sampled_at: "2026-09-18T12:00:00Z",
        received_at: `2026-09-18T12:00:${String(sequence).padStart(2, "0")}Z`,
        logical_processors: 16,
        cpu_utilization_per_mille: 375,
        memory_total_bytes: 64 * 1024 * 1024 * 1024,
        memory_available_bytes: 40 * 1024 * 1024 * 1024,
        disk_total_bytes: 2 * 1024 * 1024 * 1024 * 1024,
        disk_free_bytes: 1024 * 1024 * 1024 * 1024,
        gpu_inventory_revision: 7,
        gpus: [],
    };
}

describe("PostgreSQL managed node telemetry API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("uses the complete generation-safe history cursor", async () => {
        const cursor = telemetrySample(9);
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: [telemetrySample(8)] } as never);

        await expect(listManagedNodeTelemetry(cursor.node_id, 25, cursor)).resolves.toEqual([
            telemetrySample(8),
        ]);
        expect(axiosHttp.get).toHaveBeenCalledWith(
            `/api/console/managed/nodes/${cursor.node_id}/telemetry`,
            {
                params: {
                    limit: 25,
                    before_received_at: cursor.received_at,
                    before_generation: cursor.node_generation,
                    before_sequence: cursor.report_sequence,
                },
            },
        );
    });
});
