import { describe, expect, it } from "vitest";
import { RelayTrafficTracker } from "./relay_traffic";

function report(reportSequence: number, reportedSeconds: number, receivedBytes: number, forwardedBytes: number) {
    return {
        id: "beijing-relay",
        generation: 3,
        control_epoch: 7,
        report_sequence: reportSequence,
        last_seen: new Date(Date.UTC(2026, 9, 12) + reportedSeconds * 1_000).toISOString(),
        uploaded_bytes: receivedBytes,
        forwarded_bytes: forwardedBytes,
        fresh: true,
        state: "ready",
    };
}

describe("Relay reported traffic rates", () => {
    it("waits for two reports and uses the server interval rather than browser refresh timing", () => {
        const tracker = new RelayTrafficTracker();
        expect(tracker.observe([report(1, 0, 100, 200)], 1_000).size).toBe(0);
        const rates = tracker.observe([report(2, 5, 6_250_100, 12_500_200)], 10_000);
        expect(rates.get("beijing-relay")).toEqual({
            receivedMbps: 10,
            forwardedMbps: 20,
            observedAtMs: 10_000,
        });
    });

    it("keeps repeated and reordered reads from refreshing an old rate or baseline", () => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 0, 0, 0)], 0);
        const secondReport = report(2, 5, 6_250_000, 12_500_000);
        tracker.observe([secondReport], 5_000);
        expect(tracker.observe([report(1, 0, 0, 0)], 10_000).get("beijing-relay")?.receivedMbps).toBe(10);
        expect(tracker.observe([secondReport], 35_001).size).toBe(0);
        expect(tracker.observe([report(3, 10, 12_500_000, 25_000_000)], 36_000).size).toBe(0);
        expect(tracker.observe([report(4, 15, 18_750_000, 37_500_000)], 41_000)
            .get("beijing-relay")?.receivedMbps).toBe(10);
    });

    it.each(["generation", "control_epoch"] as const)("resets rates across a new %s", runtimeField => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 0, 0, 0)], 0);
        tracker.observe([report(2, 5, 6_250_000, 12_500_000)], 5_000);
        const restartedReport = { ...report(1, 10, 1, 2), [runtimeField]: 8 };
        expect(tracker.observe([restartedReport], 10_000).size).toBe(0);
        const followingReport = { ...report(2, 15, 6_250_001, 12_500_002), [runtimeField]: 8 };
        expect(tracker.observe([followingReport], 15_000).get("beijing-relay")?.receivedMbps).toBe(10);
    });

    it("takes a new baseline when either cumulative counter rolls back", () => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 0, 10_000_000, 20_000_000)], 0);
        expect(tracker.observe([report(2, 5, 11_000_000, 1)], 5_000).size).toBe(0);
        expect(tracker.observe([report(3, 10, 17_250_000, 12_500_001)], 10_000)
            .get("beijing-relay")?.receivedMbps).toBe(10);
    });

    it.each([0, -1, 31])("does not calculate an invalid or stale %s-second reporting interval", elapsedSeconds => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 10, 0, 0)], 0);
        expect(tracker.observe([report(2, 10 + elapsedSeconds, 100, 200)], 5_000).size).toBe(0);
    });

    it("clears missing and offline relays so a returning node needs two new reports", () => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 0, 0, 0)], 0);
        tracker.observe([report(2, 5, 100, 200)], 5_000);
        expect(tracker.observe([{ ...report(3, 10, 200, 400), fresh: false }], 10_000).size).toBe(0);
        expect(tracker.observe([report(4, 15, 300, 600)], 15_000).size).toBe(0);
        expect(tracker.observe([report(5, 20, 400, 800)], 20_000).size).toBe(1);
        expect(tracker.observe([], 25_000).size).toBe(0);
        expect(tracker.observe([report(6, 30, 500, 1_000)], 30_000).size).toBe(0);
    });

    it("shows confirmed idle traffic as zero while keeping missing or unsafe counters unknown", () => {
        const tracker = new RelayTrafficTracker();
        tracker.observe([report(1, 0, 100, 200)], 0);
        expect(tracker.observe([report(2, 5, 100, 200)], 5_000).get("beijing-relay")?.receivedMbps).toBe(0);
        expect(tracker.observe([{ ...report(3, 10, 100, 200), uploaded_bytes: null }], 10_000).size).toBe(0);
        expect(tracker.observe([report(4, 15, Number.MAX_SAFE_INTEGER + 1, 200)], 15_000).size).toBe(0);
        expect(tracker.observe([{ ...report(5, 20, 100, 200), last_seen: "invalid" }], 20_000).size).toBe(0);
    });

    it("tracks each Relay independently", () => {
        const tracker = new RelayTrafficTracker();
        const secondRelay = { ...report(1, 0, 0, 0), id: "windows-relay" };
        tracker.observe([report(1, 0, 0, 0), secondRelay], 0);
        const rates = tracker.observe([
            report(2, 5, 6_250_000, 12_500_000),
            { ...report(2, 5, 0, 0), id: secondRelay.id },
        ], 5_000);
        expect(rates.get("beijing-relay")?.forwardedMbps).toBe(20);
        expect(rates.get(secondRelay.id)?.forwardedMbps).toBe(0);
    });
});
