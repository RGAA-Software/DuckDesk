import { flushPromises, mount } from "@vue/test-utils";
import { describe, expect, it, vi } from "vitest";
import type { ManagedApplication } from "@/model/managed_application_api";
import ApplicationDeploymentsModal from "./ApplicationDeploymentsModal.vue";
vi.mock("@/model/management_events.ts", () => ({ useManagementRefresh: vi.fn() }));
vi.mock("@/model/managed_node_api", () => ({
    listManagedNodes: vi
        .fn()
        .mockResolvedValue([{ id: "node-1", public_host: "node.example", disabled: false }]),
}));
vi.mock("@/model/managed_deployment_api", () => ({
    configureManagedDeployment: vi.fn(),
    listManagedDeployments: vi.fn().mockResolvedValue([
        {
            id: "heart-deployment",
            application_id: "heart",
            node_id: "node-1",
            kind: "game_hook",
            capacity: 12,
            disabled: false,
        },
        {
            id: "other-deployment",
            application_id: "other",
            node_id: "node-1",
            kind: "webview",
            capacity: 1,
            disabled: false,
        },
    ]),
}));
vi.mock("ant-design-vue", () => ({ message: { error: vi.fn(), success: vi.fn() } }));
vi.mock("vue-i18n", () => ({ useI18n: () => ({ t: (key: string) => key }) }));
function mountDialog() {
    return mount(ApplicationDeploymentsModal, {
        props: {
            application: {
                id: "heart",
                spec: { name: "Heart", launch: { kind: "game_hook" } },
            } as ManagedApplication,
        },
        global: {
            stubs: {
                "a-modal": {
                    name: "ModalStub",
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
                "a-input": true,
                "a-form-item": true,
                "a-select": true,
                "a-input-number": true,
                "a-switch": true,
                "a-form": true,
                "a-tag": true,
                DeploymentTargetsEditor: {
                    name: "TargetsStub",
                    props: ["application", "deployments", "nodes"],
                    emits: ["busy", "back"],
                    template: "<div class='targets' />",
                },
            },
        },
    });
}
describe("application deployment dialog", () => {
    it("scopes the list and machine-set editor to the selected application", async () => {
        const dialog = mountDialog();
        await flushPromises();
        expect(dialog.get(".rows").text()).toContain("heart-deployment");
        expect(dialog.get(".rows").text()).not.toContain("other-deployment");
        await dialog.get("button").trigger("click");
        const targets = dialog.findComponent({ name: "TargetsStub" });
        expect(targets.props("application").id).toBe("heart");
        expect(targets.props("deployments")).toHaveLength(1);
        expect(targets.props("deployments")[0].capacity).toBe(12);
        dialog.unmount();
    });
    it("keeps implicit dismissal disabled and blocks closing during batch saves", async () => {
        const dialog = mountDialog();
        await flushPromises();
        const modal = dialog.findComponent({ name: "ModalStub" });
        expect(modal.props()).toMatchObject({ maskClosable: false, keyboard: false });
        await dialog.get("button").trigger("click");
        const targets = dialog.findComponent({ name: "TargetsStub" });
        targets.vm.$emit("busy", true);
        await flushPromises();
        expect(modal.props("closable")).toBe(false);
        modal.vm.$emit("cancel");
        expect(dialog.emitted("close")).toBeUndefined();
        targets.vm.$emit("busy", false);
        await flushPromises();
        modal.vm.$emit("cancel");
        expect(dialog.emitted("close")).toHaveLength(1);
        dialog.unmount();
    });
});
