import { describe, expect, it } from "vitest";

import type { NodeTelemetryHistory } from "@/model/managed_node_api.ts";
import { buildTelemetryTrend, trendSegments } from "@/model/telemetry_trend.ts";

describe("telemetry trends", () => {
    it("orders accepted samples and derives bounded machine and peak GPU pressure", () => {
        const points = buildTelemetryTrend([
            sample("2026-09-19T00:00:15Z", 500, 100, 25, [null], [null]),
            sample("2026-09-19T00:00:00Z", 250, 100, 50, [200, 700], [100, 300]),
        ]);

        expect(points).toEqual([
            {
                sampledAt: "2026-09-19T00:00:00Z",
                cpu: 250,
                memory: 500,
                disk: 500,
                gpu: 700,
                encoder: 300,
            },
            {
                sampledAt: "2026-09-19T00:00:15Z",
                cpu: 500,
                memory: 750,
                disk: 750,
                gpu: null,
                encoder: null,
            },
        ]);
    });

    it("does not draw across unknown gaps or invent a line from one sample", () => {
        expect(trendSegments([100, 200, null, 300, 400], 100, 100)).toEqual([
            "0.00,90.00 25.00,80.00",
            "75.00,70.00 100.00,60.00",
        ]);
        expect(trendSegments([null, 200, null], 100, 100)).toEqual([]);
    });
});

function sample(
    sampledAt: string,
    cpu: number | null,
    total: number,
    available: number,
    gpu: (number | null)[],
    encoder: (number | null)[],
): NodeTelemetryHistory {
    return {
        node_id: "node",
        node_generation: 1,
        report_sequence: 1,
        probe_state: "ready",
        sampled_at: sampledAt,
        received_at: sampledAt,
        logical_processors: 16,
        cpu_utilization_per_mille: cpu,
        memory_total_bytes: total,
        memory_available_bytes: available,
        disk_total_bytes: total,
        disk_free_bytes: available,
        gpu_inventory_revision: 1,
        gpus: gpu.map((utilization, index) => ({
            node_id: "node",
            node_generation: 1,
            report_sequence: 1,
            stable_key: `gpu-${index}`,
            inventory_revision: 1,
            name: `GPU ${index}`,
            dedicated_memory_bytes: 100,
            used_memory_bytes: 25,
            utilization_per_mille: utilization,
            encoder_utilization_per_mille: encoder[index] ?? null,
            sampled_at: sampledAt,
            received_at: sampledAt,
        })),
    };
}
