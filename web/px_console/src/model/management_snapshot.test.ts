import { describe, expect, it } from "vitest";
import { managementSnapshotCurrent } from "./management_snapshot";

describe("management status snapshot", () => {
    it("stops presenting an unrefreshed snapshot as current after 30 seconds", () => {
        expect(managementSnapshotCurrent(undefined, 20_000)).toBe(false);
        expect(managementSnapshotCurrent(10_000, 39_999)).toBe(true);
        expect(managementSnapshotCurrent(10_000, 40_001)).toBe(false);
        expect(managementSnapshotCurrent(10_000, 9_000)).toBe(false);
    });
});
