import { describe, expect, it } from "vitest";
import { schedulingHeadroom, type InstanceSummary } from "./dashboard_runtime";
import type { ManagedNode } from "./managed_node_api";
import type { PlacementCandidate, PlacementPreview } from "./managed_scheduling_api";

const node = {
    id: "node",
    application_port_start: 4613,
    application_port_end: 4616,
} as ManagedNode;
const candidate = (
    deploymentId: string,
    overrides: Partial<PlacementCandidate> = {},
): PlacementCandidate =>
    ({
        deployment_id: deploymentId,
        node_id: "node",
        eligible: true,
        node_slots: 3,
        deployment_slots: 2,
        ...overrides,
    }) as PlacementCandidate;
const preview = (candidates: PlacementCandidate[]) => ({ candidates }) as PlacementPreview;

describe("dashboard shared scheduling capacity", () => {
    it("deduplicates GPU alternatives and caps combined deployments by shared node slots", () => {
        expect(
            schedulingHeadroom(preview([candidate("first"), candidate("first")]), [node], []),
        ).toBe(2);
        expect(
            schedulingHeadroom(preview([candidate("first"), candidate("second")]), [node], []),
        ).toBe(3);
    });
    it("counts starting and cleanup instances as port occupancy", () => {
        const instances = [
            { node_id: "node", state: "starting", count: 2 },
            { node_id: "node", state: "stopping", count: 1 },
        ] as InstanceSummary[];
        expect(schedulingHeadroom(preview([candidate("first")]), [node], instances)).toBe(1);
    });
    it("excludes rejected candidates, handles missing ports and uses deployment capacity for RDP", () => {
        expect(
            schedulingHeadroom(preview([candidate("first", { eligible: false })]), [node], []),
        ).toBe(0);
        expect(schedulingHeadroom(preview([candidate("first")]), [], [])).toBe(0);
        expect(
            schedulingHeadroom(preview([candidate("first"), candidate("second")]), [node], []),
        ).toBe(3);
    });
    it("reports twenty available RDP user desktops from the server scheduling snapshot", () => {
        const rdpNode = { ...node, application_port_end: 4998 };
        expect(
            schedulingHeadroom(
                preview([
                    candidate("rdp", {
                        node_slots: 64,
                        deployment_slots: 20,
                    }),
                ]),
                [rdpNode],
                [],
            ),
        ).toBe(20);
    });
    it("adds independent nodes without negative capacity", () => {
        const otherNode = { ...node, id: "other-node" };
        expect(
            schedulingHeadroom(
                preview([candidate("first"), candidate("second", { node_id: otherNode.id })]),
                [node, otherNode],
                [],
            ),
        ).toBe(4);
        expect(
            schedulingHeadroom(preview([candidate("first", { node_slots: -1 })]), [node], []),
        ).toBe(0);
    });
});
