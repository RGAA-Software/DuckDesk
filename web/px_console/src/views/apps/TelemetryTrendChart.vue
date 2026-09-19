<script setup lang="ts">
import { computed } from "vue";
import { useI18n } from "vue-i18n";

import type { NodeTelemetryTrend } from "@/model/managed_node_api.ts";
import { buildAggregatedTelemetryTrend, trendSegments } from "@/model/telemetry_trend.ts";

const props = defineProps<{ trend?: NodeTelemetryTrend }>();
const { locale, t } = useI18n();
const width = 900;
const height = 220;
const seriesKeys = ["cpu", "memory", "disk", "gpu", "encoder"] as const;
const points = computed(() => buildAggregatedTelemetryTrend(props.trend?.points ?? []));
const series = computed(() =>
    seriesKeys.map(key => ({
        key,
        label: t(`nodes.trendSeries.${key}`),
        known: (props.trend?.points ?? []).reduce(
            (total, point) => total + point[`${key}_known_samples`],
            0,
        ),
        total: (props.trend?.points ?? []).reduce((total, point) => total + point.sample_count, 0),
        segments: trendSegments(
            points.value.map(point => point[key]),
            width,
            height,
        ),
    })),
);
const firstTimestamp = computed(() => formatTimestamp(points.value[0]?.sampledAt));
const lastTimestamp = computed(() => formatTimestamp(points.value.at(-1)?.sampledAt));
const hasLines = computed(() => series.value.some(item => item.segments.length > 0));

function formatTimestamp(value: string | undefined): string {
    if (!value) return "";
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "short",
        timeStyle: "medium",
    }).format(new Date(value));
}
</script>

<template>
    <section class="trend" :aria-label="t('nodes.trendTitle')">
        <div class="legend">
            <span v-for="item in series" :key="item.key" class="legend-item">
                <i :class="`swatch swatch-${item.key}`"></i>{{ item.label }}
                <small>{{
                    t("nodes.trendCoverage", { known: item.known, total: item.total })
                }}</small>
            </span>
        </div>
        <svg
            v-if="hasLines"
            class="chart"
            :viewBox="`-48 -12 ${width + 60} ${height + 42}`"
            role="img"
            :aria-label="t('nodes.trendDescription')"
            preserveAspectRatio="none"
        >
            <g class="grid">
                <line
                    v-for="level in [0, 250, 500, 750, 1000]"
                    :key="level"
                    x1="0"
                    :y1="height - (level * height) / 1000"
                    :x2="width"
                    :y2="height - (level * height) / 1000"
                />
                <text
                    v-for="level in [0, 500, 1000]"
                    :key="`label-${level}`"
                    x="-8"
                    :y="height - (level * height) / 1000 + 4"
                    text-anchor="end"
                >
                    {{ level / 10 }}%
                </text>
            </g>
            <g v-for="item in series" :key="item.key" :class="`line line-${item.key}`">
                <polyline v-for="segment in item.segments" :key="segment" :points="segment" />
            </g>
        </svg>
        <a-empty v-else :description="t('nodes.noTrend')" :image-style="{ height: '48px' }" />
        <div v-if="hasLines" class="axis-labels">
            <span>{{ firstTimestamp }}</span
            ><span>{{ lastTimestamp }}</span>
        </div>
    </section>
</template>

<style scoped>
.trend {
    margin: 16px 0;
    padding: 12px;
    color: var(--color-text);
    background: var(--color-background-soft);
    border: 1px solid var(--color-border);
    border-radius: 6px;
}
.legend {
    display: flex;
    flex-wrap: wrap;
    gap: 8px 16px;
    margin-bottom: 8px;
}
.legend-item {
    display: inline-flex;
    align-items: center;
    gap: 6px;
}
.swatch {
    width: 16px;
    height: 3px;
    border-radius: 2px;
}
.chart {
    width: 100%;
    height: 240px;
}
.grid line {
    stroke: var(--color-border);
    stroke-width: 1;
}
.grid text {
    fill: var(--color-text);
    font-size: 11px;
}
.line polyline {
    fill: none;
    stroke-width: 3;
    vector-effect: non-scaling-stroke;
}
.swatch-cpu {
    background: var(--telemetry-cpu);
}
.line-cpu polyline {
    stroke: var(--telemetry-cpu);
}
.swatch-memory {
    background: var(--telemetry-memory);
}
.line-memory polyline {
    stroke: var(--telemetry-memory);
}
.swatch-disk {
    background: var(--telemetry-disk);
}
.line-disk polyline {
    stroke: var(--telemetry-disk);
}
.swatch-gpu {
    background: var(--telemetry-gpu);
}
.line-gpu polyline {
    stroke: var(--telemetry-gpu);
}
.swatch-encoder {
    background: var(--telemetry-encoder);
}
.line-encoder polyline {
    stroke: var(--telemetry-encoder);
}
.axis-labels {
    display: flex;
    justify-content: space-between;
    gap: 16px;
    font-size: 12px;
}
</style>
