import { flushPromises, mount } from "@vue/test-utils";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { ManagedApplication } from "@/model/managed_application_api";
import { listManagedNodes } from "@/model/managed_node_api";
import {
    createManagedDeployment,
    listManagedDeployments,
    type ManagedDeployment,
} from "@/model/managed_deployment_api";
import ApplicationDeploymentsModal from "./ApplicationDeploymentsModal.vue";

vi.mock("@/model/management_events.ts", () => ({ useManagementRefresh: vi.fn() }));
vi.mock("@/model/managed_node_api", () => ({ listManagedNodes: vi.fn() }));
vi.mock("@/model/managed_deployment_api", () => ({
    listManagedDeployments: vi.fn(),
    createManagedDeployment: vi.fn(),
    configureManagedDeployment: vi.fn(),
}));
vi.mock("ant-design-vue", () => ({ message: { error: vi.fn(), success: vi.fn() } }));
vi.mock("vue-i18n", () => ({ useI18n: () => ({ t: (key: string) => key }) }));

const application = {
    id: "heart",
    spec: { name: "Heart", disabled: false, launch: { kind: "game_hook" } },
} as ManagedApplication;
const deployments = [
    {
        id: "heart-deployment",
        application_id: "heart",
        node_id: "node-1",
        kind: "game_hook",
        capacity: 1,
    },
    {
        id: "water-deployment",
        application_id: "water",
        node_id: "node-1",
        kind: "webview",
        capacity: 1,
    },
] as ManagedDeployment[];
function mountDeployments(selectedApplication = application) {
    return mount(ApplicationDeploymentsModal, {
        props: { application: selectedApplication },
        global: {
            stubs: {
                "a-modal": {
                    name: "DeploymentModalStub",
                    props: ["maskClosable", "keyboard", "closable"],
                    emits: ["cancel"],
                    template: "<section><slot /><footer><slot name='footer' /></footer></section>",
                },
                "a-button": {
                    props: ["disabled"],
                    template: "<button :disabled='disabled'><slot /></button>",
                },
                "a-alert": {
                    props: ["message"],
                    template: "<aside>{{ message }}<slot name='action' /></aside>",
                },
                "a-space": { template: "<div><slot /></div>" },
                "a-table": {
                    props: ["dataSource"],
                    template: "<div class='rows'>{{ dataSource }}</div>",
                },
                "a-table-column": true,
                "a-form": { template: "<form><slot /></form>" },
                "a-form-item": { template: "<label><slot /></label>" },
                "a-input": {
                    props: ["value", "disabled"],
                    template: "<input :value='value' :disabled='disabled' />",
                },
                "a-select": true,
                "a-input-number": true,
                "a-switch": true,
                "a-tag": true,
            },
        },
    });
}
beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(listManagedDeployments).mockResolvedValue(deployments);
    vi.mocked(listManagedNodes).mockResolvedValue([
        { id: "node-1", public_host: "node.example", disabled: false },
        { id: "node-2", public_host: "second-node.example", disabled: false },
    ] as Awaited<ReturnType<typeof listManagedNodes>>);
    vi.mocked(createManagedDeployment).mockResolvedValue(deployments[0]!);
});
describe("application deployment dialog", () => {
    it("explains why creation is unavailable when every node already has this application", async () => {
        vi.mocked(listManagedNodes).mockResolvedValue([
            { id: "node-1", public_host: "node.example", disabled: false },
        ] as Awaited<ReturnType<typeof listManagedNodes>>);
        const dialog = mountDeployments();
        await flushPromises();
        expect(dialog.text()).toContain("deployments.allNodesDeployed");
        expect(dialog.get("button").element.disabled).toBe(true);
        await dialog.get("button").trigger("click");
        expect(dialog.find("form").exists()).toBe(false);
        expect(createManagedDeployment).not.toHaveBeenCalled();
        dialog.unmount();
    });
    it("recovers a concurrent duplicate as an actionable existing deployment", async () => {
        const dialog = mountDeployments();
        await flushPromises();
        await dialog.get("button").trigger("click");
        vi.mocked(listManagedDeployments).mockResolvedValue([
            ...deployments,
            { ...deployments[0]!, id: "concurrent-deployment", node_id: "node-2", capacity: 12 },
        ]);
        vi.mocked(createManagedDeployment).mockRejectedValueOnce({
            isAxiosError: true,
            response: { status: 409 },
        });
        await dialog.findAll("footer button")[1]!.trigger("click");
        await flushPromises();
        expect(dialog.text()).toContain("deployments.duplicate");
        expect(dialog.findAll("footer button")[1]!.attributes("disabled")).toBeDefined();
        await dialog.get("aside button").trigger("click");
        expect(dialog.text()).not.toContain("deployments.duplicate");
        expect(dialog.find("a-switch-stub").exists()).toBe(true);
        expect(createManagedDeployment).toHaveBeenCalledTimes(1);
        dialog.unmount();
    });
    it.each([
        [401, "sessionExpired"],
        [403, "forbidden"],
        [400, "invalidConfiguration"],
        [409, "configurationConflict"],
    ])("explains HTTP %s when no duplicate is confirmed", async (status, key) => {
        const dialog = mountDeployments();
        await flushPromises();
        await dialog.get("button").trigger("click");
        vi.mocked(createManagedDeployment).mockRejectedValueOnce({
            isAxiosError: true,
            response: { status },
        });
        await dialog.findAll("footer button")[1]!.trigger("click");
        await flushPromises();
        expect(dialog.text()).toContain(`deployments.${key}`);
        expect(dialog.text()).not.toContain("deployments.duplicate");
        dialog.unmount();
    });
    it("shows only the selected application's deployments and disables implicit dismissal", async () => {
        const dialog = mountDeployments();
        await flushPromises();
        expect(dialog.get(".rows").text()).toContain("heart-deployment");
        expect(dialog.get(".rows").text()).not.toContain("water-deployment");
        expect(dialog.findComponent({ name: "DeploymentModalStub" }).props()).toMatchObject({
            maskClosable: false,
            keyboard: false,
        });
        await dialog.get("footer button").trigger("click");
        expect(dialog.emitted("close")).toHaveLength(1);
        dialog.unmount();
    });
    it("binds creation to the selected application and retains the editor on failure", async () => {
        vi.mocked(createManagedDeployment).mockRejectedValueOnce(new Error("rejected"));
        const dialog = mountDeployments();
        await flushPromises();
        await dialog.get("button").trigger("click");
        expect(dialog.get("input").element.disabled).toBe(true);
        expect(dialog.get("input").element.value).toBe("Heart");
        await dialog.findAll("footer button")[1]!.trigger("click");
        await flushPromises();
        expect(createManagedDeployment).toHaveBeenCalledWith("heart", "node-2", {
            target: { kind: "game_hook" },
            gpu_key: null,
            capacity: 1,
            disabled: false,
        });
        expect(dialog.text()).toContain("deployments.saveFailed");
        expect(dialog.find("form").exists()).toBe(true);
        await dialog.findAll("footer button")[1]!.trigger("click");
        await flushPromises();
        expect(dialog.find("form").exists()).toBe(false);
        dialog.unmount();
    });
    it("keeps RDP capacity at one without a GPU binding", async () => {
        const dialog = mountDeployments({
            ...application,
            id: "rdp-app",
            spec: { ...application.spec, launch: { kind: "rdp" } },
        });
        await flushPromises();
        await dialog.get("button").trigger("click");
        await dialog.findAll("footer button")[1]!.trigger("click");
        await flushPromises();
        expect(createManagedDeployment).toHaveBeenCalledWith("rdp-app", "node-1", {
            target: { kind: "rdp" },
            gpu_key: null,
            capacity: 1,
            disabled: false,
        });
        dialog.unmount();
    });
    it("prevents closing while saving and ignores a late result after disposal", async () => {
        let finishSave: (deployment: ManagedDeployment) => void = () => {};
        vi.mocked(createManagedDeployment).mockImplementationOnce(
            () =>
                new Promise(resolve => {
                    finishSave = resolve;
                }),
        );
        const dialog = mountDeployments();
        await flushPromises();
        await dialog.get("button").trigger("click");
        await dialog.findAll("footer button")[1]!.trigger("click");
        const modal = dialog.findComponent({ name: "DeploymentModalStub" });
        expect(modal.props("closable")).toBe(false);
        modal.vm.$emit("cancel");
        expect(dialog.emitted("close")).toBeUndefined();
        dialog.unmount();
        finishSave(deployments[0]!);
        await flushPromises();
        expect(listManagedDeployments).toHaveBeenCalledTimes(1);
    });
});
