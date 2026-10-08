import { flushPromises, mount } from "@vue/test-utils";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { previewPlacement, type PlacementPreview } from "@/model/managed_scheduling_api";
import { listManagedApplications } from "@/model/managed_application_api";
import SchedulingPreviewCard from "./SchedulingPreviewCard.vue";

vi.mock("@/model/management_events.ts", () => ({ useManagementRefresh: vi.fn() }));
vi.mock("@/model/managed_application_api", () => ({ listManagedApplications: vi.fn() }));
vi.mock("@/model/managed_deployment_api", () => ({
    listManagedDeployments: vi.fn().mockResolvedValue([]),
}));
vi.mock("@/model/managed_node_api", () => ({ listManagedNodes: vi.fn().mockResolvedValue([]) }));
vi.mock("@/model/managed_scheduling_api", () => ({ previewPlacement: vi.fn() }));
vi.mock("vue-i18n", () => ({ useI18n: () => ({ t: (key: string) => key }) }));

const emptyPreview: PlacementPreview = {
    application_id: "heart",
    evaluated_at: "2026-10-08T00:00:00Z",
    candidates: [],
};
function mountPreview() {
    return mount(SchedulingPreviewCard, {
        global: {
            stubs: {
                "a-card": { template: "<section><slot /></section>" },
                "a-space": { template: "<div><slot /></div>" },
                "a-button": { template: "<button><slot /></button>" },
                "a-alert": {
                    props: ["message", "description"],
                    template:
                        "<aside>{{ message }} {{ description }}<slot name='action' /></aside>",
                },
                "a-select": {
                    props: ["value"],
                    emits: ["update:value"],
                    template:
                        "<input :value='value' @input='$emit(\"update:value\", $event.target.value)' />",
                },
                "a-table": {
                    props: ["dataSource"],
                    template: "<div class='results'>{{ dataSource }}</div>",
                },
                "a-table-column": true,
                "a-tag": true,
            },
        },
    });
}
beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(listManagedApplications).mockResolvedValue([
        { id: "heart", spec: { name: "Heart" } },
        { id: "water", spec: { name: "Water" } },
    ] as Awaited<ReturnType<typeof listManagedApplications>>);
    vi.mocked(previewPlacement).mockResolvedValue(emptyPreview);
});
describe("scheduling preview feedback", () => {
    it("shows request failures and allows retry with an explicit empty result", async () => {
        vi.mocked(previewPlacement).mockRejectedValueOnce(new Error("Network failure"));
        const card = mountPreview();
        await flushPromises();
        await card.get("button").trigger("click");
        await flushPromises();
        expect(card.text()).toContain("scheduling.failed");
        await card.get("button").trigger("click");
        await flushPromises();
        expect(card.text()).toContain("scheduling.noCandidates");
        expect(card.text()).not.toContain("scheduling.failed");
        card.unmount();
    });
    it.each([
        [401, "sessionExpired"],
        [403, "forbidden"],
        [404, "applicationMissing"],
    ])("explains HTTP %s", async (status, key) => {
        vi.mocked(previewPlacement).mockRejectedValueOnce({
            isAxiosError: true,
            response: { status },
        });
        const card = mountPreview();
        await flushPromises();
        await card.get("button").trigger("click");
        await flushPromises();
        expect(card.text()).toContain(`scheduling.${key}`);
        card.unmount();
    });
    it("discards a late response after changing deployment and keeps the newer result", async () => {
        let finishPrevious: (preview: PlacementPreview) => void = () => {};
        vi.mocked(previewPlacement).mockImplementationOnce(
            () =>
                new Promise(resolve => {
                    finishPrevious = resolve;
                }),
        );
        const card = mountPreview();
        await flushPromises();
        await card.get("button").trigger("click");
        expect(card.text()).toContain("scheduling.checking");
        await card.findAll("input")[1]!.setValue("deployment-new");
        expect(card.text()).toContain("scheduling.idle");
        await card.get("button").trigger("click");
        await flushPromises();
        finishPrevious({
            ...emptyPreview,
            candidates: [{ eligible: true, rank: 1, node_id: "old-node" }],
        } as PlacementPreview);
        await flushPromises();
        expect(card.text()).toContain("scheduling.noCandidates");
        expect(card.text()).not.toContain("old-node");
        card.unmount();
    });
    it("explains catalog loading failure and recovers through retry", async () => {
        vi.mocked(listManagedApplications).mockRejectedValueOnce(new Error("offline"));
        const card = mountPreview();
        await flushPromises();
        expect(card.text()).toContain("scheduling.catalogFailed");
        await card.findAll("button")[1]!.trigger("click");
        await flushPromises();
        expect(card.text()).not.toContain("scheduling.catalogFailed");
        expect(card.get("input").element.value).toBe("heart");
        card.unmount();
    });
});
