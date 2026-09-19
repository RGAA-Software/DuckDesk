import { describe, expect, it } from "vitest";

import type { NodeTelemetryHistory, NodeTelemetryTrendPoint } from "@/model/managed_node_api.ts";
import {
    buildAggregatedTelemetryTrend,
    buildTelemetryTrend,
    trendSegments,
} from "@/model/telemetry_trend.ts";

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

    it("preserves empty and unknown server buckets instead of inventing zero utilization", () => {
        expect(
            buildAggregatedTelemetryTrend([
                aggregatePoint("2026-09-19T00:00:00Z", 0, 0, null),
                aggregatePoint("2026-09-19T00:01:00Z", 2, 0, null),
                aggregatePoint("2026-09-19T00:02:00Z", 2, 2, 425),
            ]),
        ).toEqual([
            {
                sampledAt: "2026-09-19T00:00:00Z",
                cpu: null,
                memory: null,
                disk: null,
                gpu: null,
                encoder: null,
            },
            {
                sampledAt: "2026-09-19T00:01:00Z",
                cpu: null,
                memory: null,
                disk: null,
                gpu: null,
                encoder: null,
            },
            {
                sampledAt: "2026-09-19T00:02:00Z",
                cpu: 425,
                memory: 425,
                disk: 425,
                gpu: 425,
                encoder: 425,
            },
        ]);
    });
});

function aggregatePoint(
    bucketStart: string,
    sampleCount: number,
    knownSamples: number,
    average: number | null,
): NodeTelemetryTrendPoint {
    return {
        bucket_start: bucketStart,
        sample_count: sampleCount,
        cpu_known_samples: knownSamples,
        cpu_average_per_mille: average,
        memory_known_samples: knownSamples,
        memory_average_per_mille: average,
        disk_known_samples: knownSamples,
        disk_average_per_mille: average,
        gpu_known_samples: knownSamples,
        gpu_average_per_mille: average,
        encoder_known_samples: knownSamples,
        encoder_average_per_mille: average,
    };
}

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
            runtime_binding_ready: true,
            dedicated_memory_bytes: 100,
            used_memory_bytes: 25,
            utilization_per_mille: utilization,
            encoder_utilization_per_mille: encoder[index] ?? null,
            sampled_at: sampledAt,
            received_at: sampledAt,
        })),
    };
}
