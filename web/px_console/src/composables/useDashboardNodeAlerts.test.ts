import { flushPromises, mount } from "@vue/test-utils";
import { defineComponent } from "vue";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { listManagedDevices } from "@/model/managed_device_api";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import { listTelemetryAlerts, type TelemetryAlertEvent } from "@/model/telemetry_alert_api";
import { useDashboardNodeAlerts } from "./useDashboardNodeAlerts";

vi.mock("@/model/managed_device_api", () => ({ listManagedDevices: vi.fn() }));
vi.mock("@/model/managed_node_api", () => ({ listManagedNodes: vi.fn() }));
vi.mock("@/model/telemetry_alert_api", () => ({ listTelemetryAlerts: vi.fn() }));
vi.mock("@/model/management_events", () => ({ useManagementRefresh: vi.fn() }));

function node(id: string, fresh: boolean, disabled = false): ManagedNode {
    return {
        id,
        device_id: "device",
        fresh,
        disabled,
        last_seen: null,
        public_host: "node.example",
    } as ManagedNode;
}
function event(id: string, state: TelemetryAlertEvent["state"]): TelemetryAlertEvent {
    return {
        id,
        node_id: "ready",
        state,
        severity: "warning",
        last_sampled_at: "2026-10-08T01:00:00Z",
    } as TelemetryAlertEvent;
}
function mountAlerts() {
    let model!: ReturnType<typeof useDashboardNodeAlerts>;
    const wrapper = mount(
        defineComponent({
            setup() {
                model = useDashboardNodeAlerts();
                return () => null;
            },
        }),
    );
    return {
        wrapper,
        get model() {
            return model;
        },
    };
}

describe("dashboard node alerts", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.mocked(listManagedNodes).mockResolvedValue([]);
        vi.mocked(listManagedDevices).mockResolvedValue([]);
        vi.mocked(listTelemetryAlerts).mockResolvedValue([]);
    });
    afterEach(() => vi.useRealTimers());

    it("keeps acknowledged unresolved alerts, deduplicates transitions, and flags only enabled offline nodes", async () => {
        vi.mocked(listManagedNodes).mockResolvedValue([
            node("ready", true),
            node("offline", false),
            node("disabled", false, true),
        ]);
        vi.mocked(listManagedDevices).mockResolvedValue([
            { id: "device", name: "Machine 90" } as Awaited<
                ReturnType<typeof listManagedDevices>
            >[number],
        ]);
        vi.mocked(listTelemetryAlerts)
            .mockResolvedValueOnce([event("changing", "active"), event("recovered", "recovered")])
            .mockResolvedValueOnce([
                event("changing", "acknowledged"),
                event("acknowledged", "acknowledged"),
            ]);
        const fixture = mountAlerts();
        try {
            await flushPromises();
            expect(listTelemetryAlerts).toHaveBeenCalledWith({ state: "active" }, 100, undefined);
            expect(listTelemetryAlerts).toHaveBeenCalledWith(
                { state: "acknowledged" },
                100,
                undefined,
            );
            expect(fixture.model.alerts.value.map(alert => alert.id)).toEqual([
                "offline:offline",
                "changing",
                "acknowledged",
            ]);
            expect(fixture.model.alerts.value.every(alert => alert.nodeName === "Machine 90")).toBe(
                true,
            );
            expect(fixture.model.alerts.value[1]?.event?.state).toBe("acknowledged");
        } finally {
            fixture.wrapper.unmount();
        }
    });

    it("counts all alert pages instead of treating the first 100 as the total", async () => {
        const firstPage = Array.from({ length: 100 }, (_, alertIndex) =>
            event(`active-${alertIndex}`, "active"),
        );
        vi.mocked(listTelemetryAlerts).mockImplementation(async (filters, _limit, before) => {
            if (filters?.state === "acknowledged") return [event("acknowledged", "acknowledged")];
            return before ? [event("last-active", "active")] : firstPage;
        });
        const fixture = mountAlerts();
        try {
            await flushPromises();
            expect(fixture.model.alerts.value).toHaveLength(102);
            expect(listTelemetryAlerts).toHaveBeenCalledWith(
                { state: "active" },
                100,
                firstPage.at(-1),
            );
            expect(fixture.model.loadFailed.value).toBe(false);
        } finally {
            fixture.wrapper.unmount();
        }
    });

    it("does not publish a partial total when an additional page fails", async () => {
        const firstPage = Array.from({ length: 100 }, (_, alertIndex) =>
            event(`active-${alertIndex}`, "active"),
        );
        vi.mocked(listTelemetryAlerts).mockImplementation(async (filters, _limit, before) => {
            if (filters?.state === "acknowledged") return [];
            if (before) throw new Error("unavailable");
            return firstPage;
        });
        const fixture = mountAlerts();
        try {
            await flushPromises();
            expect(fixture.model.alerts.value).toEqual([]);
            expect(fixture.model.loadFailed.value).toBe(true);
        } finally {
            fixture.wrapper.unmount();
        }
    });

    it("reports failed refreshes instead of healthy status and recovers on retry", async () => {
        vi.mocked(listTelemetryAlerts).mockRejectedValueOnce(new Error("unavailable"));
        const fixture = mountAlerts();
        try {
            await flushPromises();
            expect(fixture.model.loadFailed.value).toBe(true);
            await fixture.model.refresh();
            expect(fixture.model.loadFailed.value).toBe(false);
            expect(fixture.model.loading.value).toBe(false);
        } finally {
            fixture.wrapper.unmount();
        }
    });

    it("ignores late responses after disposal and removes the refresh timer", async () => {
        vi.useFakeTimers();
        let resolveNodes!: (nodes: ManagedNode[]) => void;
        vi.mocked(listManagedNodes).mockReturnValue(
            new Promise(resolve => {
                resolveNodes = resolve;
            }),
        );
        const fixture = mountAlerts();
        await fixture.model.refresh();
        expect(listManagedNodes).toHaveBeenCalledOnce();
        fixture.wrapper.unmount();
        resolveNodes([node("offline", false)]);
        await Promise.resolve();
        await Promise.resolve();
        expect(fixture.model.alerts.value).toEqual([]);
        await vi.advanceTimersByTimeAsync(60_000);
        expect(listManagedNodes).toHaveBeenCalledOnce();
    });
});
