import type { NodeTelemetryHistory } from "@/model/managed_node_api.ts";

export interface TelemetryTrendPoint {
    sampledAt: string;
    cpu: number | null;
    memory: number | null;
    disk: number | null;
    gpu: number | null;
    encoder: number | null;
}

export function buildTelemetryTrend(
    samples: readonly NodeTelemetryHistory[],
): TelemetryTrendPoint[] {
    return [...samples]
        .sort((left, right) => Date.parse(left.sampled_at) - Date.parse(right.sampled_at))
        .map(sample => ({
            sampledAt: sample.sampled_at,
            cpu: boundedPerMille(sample.cpu_utilization_per_mille),
            memory: usedPerMille(sample.memory_total_bytes, sample.memory_available_bytes),
            disk: usedPerMille(sample.disk_total_bytes, sample.disk_free_bytes),
            gpu: maximumPerMille(sample.gpus.map(gpu => gpu.utilization_per_mille)),
            encoder: maximumPerMille(sample.gpus.map(gpu => gpu.encoder_utilization_per_mille)),
        }));
}

export function trendSegments(
    values: readonly (number | null)[],
    width: number,
    height: number,
): string[] {
    if (values.length < 2 || width <= 0 || height <= 0) return [];
    const segments: string[] = [];
    let current: string[] = [];
    values.forEach((value, index) => {
        if (value === null) {
            if (current.length > 1) segments.push(current.join(" "));
            current = [];
            return;
        }
        const x = (index * width) / (values.length - 1);
        const y = height - (value * height) / 1000;
        current.push(`${x.toFixed(2)},${y.toFixed(2)}`);
    });
    if (current.length > 1) segments.push(current.join(" "));
    return segments;
}

function boundedPerMille(value: number | null): number | null {
    return value !== null && Number.isInteger(value) && value >= 0 && value <= 1000 ? value : null;
}

function usedPerMille(total: number | null, available: number | null): number | null {
    if (
        total === null ||
        available === null ||
        !Number.isFinite(total) ||
        !Number.isFinite(available) ||
        total <= 0 ||
        available < 0 ||
        available > total
    ) {
        return null;
    }
    return Math.round(((total - available) * 1000) / total);
}

function maximumPerMille(values: readonly (number | null)[]): number | null {
    const known = values.map(boundedPerMille).filter((value): value is number => value !== null);
    return known.length > 0 ? Math.max(...known) : null;
}
