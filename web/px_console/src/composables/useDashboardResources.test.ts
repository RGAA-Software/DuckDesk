import { defineComponent } from "vue";
import { flushPromises, mount } from "@vue/test-utils";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { listAllAdminUsers } from "@/model/identity_api";
import { listManagedDevices } from "@/model/managed_device_api";
import { listManagedNodes } from "@/model/managed_node_api";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedDeployments } from "@/model/managed_deployment_api";
import { listManagedResourceSessions } from "@/model/managed_activity_api";
import { previewPlacement } from "@/model/managed_scheduling_api";
import { getInstanceSummary } from "@/model/dashboard_runtime";
import { useDashboardResources } from "./useDashboardResources";

vi.mock("@/model/identity_api", () => ({ listAllAdminUsers: vi.fn() }));
vi.mock("@/model/managed_device_api", () => ({ listManagedDevices: vi.fn() }));
vi.mock("@/model/managed_node_api", () => ({ listManagedNodes: vi.fn() }));
vi.mock("@/model/managed_application_api", () => ({ listManagedApplications: vi.fn() }));
vi.mock("@/model/managed_deployment_api", () => ({ listManagedDeployments: vi.fn() }));
vi.mock("@/model/managed_activity_api", () => ({ listManagedResourceSessions: vi.fn() }));
vi.mock("@/model/managed_scheduling_api", () => ({ previewPlacement: vi.fn() }));
vi.mock("@/model/dashboard_runtime", () => ({ getInstanceSummary: vi.fn() }));
vi.mock("@/model/management_events", () => ({ useManagementRefresh: vi.fn() }));

function mountResources() {
    let resources!: ReturnType<typeof useDashboardResources>;
    const wrapper = mount(
        defineComponent({
            setup() {
                resources = useDashboardResources();
                return () => null;
            },
        }),
    );
    return { wrapper, resources };
}

describe("dashboard snapshot lifecycle", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.useFakeTimers({ toFake: ["setInterval", "clearInterval", "Date", "performance"] });
        vi.mocked(listAllAdminUsers).mockResolvedValue([]);
        vi.mocked(listManagedDevices).mockResolvedValue([]);
        vi.mocked(listManagedNodes).mockResolvedValue([]);
        vi.mocked(listManagedApplications).mockResolvedValue([]);
        vi.mocked(listManagedDeployments).mockResolvedValue([]);
        vi.mocked(listManagedResourceSessions).mockResolvedValue([]);
        vi.mocked(getInstanceSummary).mockResolvedValue([]);
    });
    afterEach(() => {
        vi.useRealTimers();
    });
    it("invalidates a failed refresh and recovers without displaying false zeros", async () => {
        const { wrapper, resources } = mountResources();
        await flushPromises();
        expect(resources.current.value).toBe(true);
        vi.mocked(getInstanceSummary).mockRejectedValueOnce(new Error("unavailable"));
        await resources.refresh();
        expect(resources.current.value).toBe(false);
        expect(resources.loadFailed.value).toBe(true);
        await resources.refresh();
        expect(resources.current.value).toBe(true);
        expect(resources.loadFailed.value).toBe(false);
        wrapper.unmount();
    });
    it("does not apply responses or poll after unmount", async () => {
        let resolveUsers!: (users: Awaited<ReturnType<typeof listAllAdminUsers>>) => void;
        vi.mocked(listAllAdminUsers).mockReturnValueOnce(
            new Promise(resolve => {
                resolveUsers = resolve;
            }),
        );
        const { wrapper, resources } = mountResources();
        await resources.refresh();
        expect(listAllAdminUsers).toHaveBeenCalledTimes(1);
        wrapper.unmount();
        resolveUsers([]);
        await flushPromises();
        await vi.advanceTimersByTimeAsync(60_000);
        expect(resources.current.value).toBe(false);
        expect(listAllAdminUsers).toHaveBeenCalledTimes(1);
    });
    it("expires a snapshot while a later refresh is stuck", async () => {
        const { wrapper, resources } = mountResources();
        await flushPromises();
        vi.mocked(listAllAdminUsers).mockReturnValueOnce(new Promise(() => {}));
        await vi.advanceTimersByTimeAsync(31_000);
        expect(resources.current.value).toBe(false);
        expect(listAllAdminUsers).toHaveBeenCalledTimes(2);
        wrapper.unmount();
    });
    it("keeps metrics available when one application preview fails", async () => {
        vi.mocked(listManagedApplications).mockResolvedValue([
            { id: "first" },
            { id: "second" },
        ] as ManagedApplication[]);
        vi.mocked(previewPlacement)
            .mockRejectedValueOnce(new Error("preview failed"))
            .mockResolvedValueOnce({
                application_id: "second",
                evaluated_at: new Date().toISOString(),
                candidates: [],
            });
        const { wrapper, resources } = mountResources();
        await flushPromises();
        expect(resources.current.value).toBe(true);
        expect(resources.previews.value.first).toBeUndefined();
        expect(resources.previews.value.second?.candidates).toEqual([]);
        wrapper.unmount();
    });
});
