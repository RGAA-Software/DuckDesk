import { describe, expect, it } from "vitest";
import { relayBandwidthState } from "./relay_bandwidth";

describe("Relay bandwidth admission", () => {
    it("distinguishes unknown samples from configured zero traffic", () => {
        expect(
            relayBandwidthState({ bandwidth_capacity_mbps: null, forwarded_rate_bps: null }),
        ).toBe("unconfigured");
        expect(relayBandwidthState({ bandwidth_capacity_mbps: 20, forwarded_rate_bps: null })).toBe(
            "unknown",
        );
        expect(relayBandwidthState({ bandwidth_capacity_mbps: 20, forwarded_rate_bps: 0 })).toBe(
            "available",
        );
    });
    it("uses the same ten percent headroom as Console selection", () => {
        expect(
            relayBandwidthState({ bandwidth_capacity_mbps: 20, forwarded_rate_bps: 17_999_999 }),
        ).toBe("available");
        expect(
            relayBandwidthState({ bandwidth_capacity_mbps: 20, forwarded_rate_bps: 18_000_000 }),
        ).toBe("busy");
        expect(
            relayBandwidthState({ bandwidth_capacity_mbps: 20, forwarded_rate_bps: 25_000_000 }),
        ).toBe("busy");
    });
    it("does not turn corrupt capacities and counters into available headroom", () => {
        for (const bandwidthCapacity of [0, -1, 1.5, NaN, 100001]) {
            expect(
                relayBandwidthState({
                    bandwidth_capacity_mbps: bandwidthCapacity,
                    forwarded_rate_bps: 0,
                }),
            ).toBe("unknown");
        }
        for (const forwardedRate of [-1, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1]) {
            expect(
                relayBandwidthState({
                    bandwidth_capacity_mbps: 20,
                    forwarded_rate_bps: forwardedRate,
                }),
            ).toBe("unknown");
        }
    });
});
