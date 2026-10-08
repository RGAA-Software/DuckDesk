import { flushPromises, mount } from "@vue/test-utils";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
    configureManagedNode,
    createManagedNode,
    listManagedNodes,
    type ManagedNode,
} from "@/model/managed_node_api";
import type { ManagedDevice } from "@/model/managed_device_api";
import DeviceRuntimeDetails from "./DeviceRuntimeDetails.vue";

vi.mock("@/model/management_events.ts", () => ({ useManagementRefresh: vi.fn() }));
vi.mock("@/model/managed_node_api", () => ({
    listManagedNodes: vi.fn(),
    configureManagedNode: vi.fn(),
    createManagedNode: vi.fn(),
    deleteManagedNode: vi.fn(),
    getManagedNodeTelemetryTrend: vi.fn(),
    listManagedNodeTelemetry: vi.fn(),
}));
vi.mock("ant-design-vue", () => ({
    Modal: { confirm: vi.fn() },
    message: { error: vi.fn(), success: vi.fn() },
}));
vi.mock("vue-i18n", () => ({
    useI18n: () => ({ locale: { value: "en" }, t: (key: string) => key }),
}));

const selectedDevice: ManagedDevice = {
    id: "selected-device",
    public_code: "123456",
    name: "Selected workstation",
    platform: "windows",
    disabled: false,
    revision: 1,
    registered_at: "2026-10-08T00:00:00Z",
};
function nodeFor(deviceId: string): ManagedNode {
    return {
        id: "node-" + deviceId,
        device_id: deviceId,
        product: "cloud_node",
        revision: 7,
        generation: 1,
        control_epoch: 1,
        state: "ready",
        draining: false,
        disabled: false,
        max_instances: 4,
        report_sequence: 1,
        last_seen: null,
        product_version_code: 30397,
        public_host: deviceId + ".example",
        desktop_port: 4601,
        application_port_start: 4613,
        application_port_end: 4998,
        game_hook: true,
        webview: true,
        rdp: true,
        endpoint_revision: 1,
        fresh: true,
        telemetry: null,
        gpus: [],
    };
}
function mountDetails() {
    return mount(DeviceRuntimeDetails, {
        props: { device: selectedDevice },
        global: {
            stubs: {
                "a-spin": { template: "<section><slot/></section>" },
                "a-space": { template: "<div><slot/></div>" },
                "a-tag": { template: "<span><slot/></span>" },
                "a-button": { template: "<button><slot/></button>" },
                "a-descriptions": { template: "<dl><slot/></dl>" },
                "a-descriptions-item": { template: "<dd><slot/></dd>" },
                "a-empty": { template: "<section><slot/></section>" },
                "a-modal": {
                    props: ["open"],
                    emits: ["ok"],
                    template:
                        '<section v-if="open"><slot/><button class="save" @click="$emit(\'ok\')">Save</button></section>',
                },
                "a-form": { template: "<form><slot/></form>" },
                "a-form-item": { template: "<label><slot/></label>" },
                "a-input": true,
                "a-select": true,
                "a-input-number": true,
                "a-switch": true,
                "a-table": true,
                "a-table-column": true,
                "a-alert": true,
                "a-typography-paragraph": true,
                TelemetryTrendChart: true,
            },
        },
    });
}

describe("device runtime ownership", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.useFakeTimers();
    });
    afterEach(() => vi.useRealTimers());

    it("shows and configures only the selected device and releases its status timer", async () => {
        const selectedNode = nodeFor(selectedDevice.id);
        vi.mocked(listManagedNodes).mockResolvedValue([nodeFor("unrelated-device"), selectedNode]);
        const details = mountDetails();
        await flushPromises();
        expect(details.text()).toContain("selected-device.example:4601");
        expect(details.text()).not.toContain("unrelated-device.example");
        await details
            .findAll("button")
            .find(button => button.text() === "nodes.edit")!
            .trigger("click");
        await details.find(".save").trigger("click");
        await flushPromises();
        expect(configureManagedNode).toHaveBeenCalledWith(selectedNode, {
            draining: false,
            disabled: false,
            max_instances: 4,
        });
        expect(details.emitted("changed")).toHaveLength(1);
        details.unmount();
        expect(vi.getTimerCount()).toBe(0);
    });

    it("registers an unassigned selected device without adopting another device's runtime", async () => {
        vi.mocked(listManagedNodes).mockResolvedValue([nodeFor("unrelated-device")]);
        vi.mocked(createManagedNode).mockResolvedValue({
            node: nodeFor(selectedDevice.id),
            node_token: "test-enrollment-token",
        });
        const details = mountDetails();
        await flushPromises();
        expect(details.text()).not.toContain("unrelated-device.example");
        await details
            .findAll("button")
            .find(button => button.text() === "nodes.create")!
            .trigger("click");
        await details.find(".save").trigger("click");
        await flushPromises();
        expect(createManagedNode).toHaveBeenCalledWith(selectedDevice.id, "cloud_node", 4);
        expect(configureManagedNode).not.toHaveBeenCalled();
        details.unmount();
        expect(vi.getTimerCount()).toBe(0);
    });

    it("does not offer registration when inventory loading fails", async () => {
        vi.mocked(listManagedNodes).mockRejectedValue(new Error("offline"));
        const details = mountDetails();
        await flushPromises();
        expect(details.findAll("button").some(button => button.text() === "nodes.create")).toBe(
            false,
        );
        details.unmount();
    });
});
