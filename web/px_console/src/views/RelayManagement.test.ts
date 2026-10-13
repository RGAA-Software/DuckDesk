import { flushPromises, mount, type VueWrapper } from "@vue/test-utils";
import { Table, TableColumn } from "ant-design-vue";
import { createI18n } from "vue-i18n";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { listManagedRelays, type ManagedRelay } from "@/model/managed_relay_api";
import { useManagementRefresh } from "@/model/management_events";
import en from "@/locales/en";
import zh from "@/locales/zh";
import RelayManagement from "./RelayManagement.vue";

vi.mock("@/model/managed_relay_api", () => ({
    listManagedRelays: vi.fn(), createManagedRelay: vi.fn(), configureManagedRelay: vi.fn(),
}));
vi.mock("@/model/management_events", () => ({ useManagementRefresh: vi.fn() }));

function relayReport(sequence: number): ManagedRelay {
    return {
        id: "beijing-relay", name: "Beijing", public_host: "relay.example.test", public_port: 4605,
        revision: 1, generation: 3, control_epoch: 7, state: "ready",
        desired_draining: false, reported_draining: false, disabled: false,
        report_sequence: sequence,
        last_seen: new Date(Date.UTC(2026, 9, 12) + sequence * 5_000).toISOString(),
        product_version_code: 100056, max_connections: 100, current_connections: 2,
        max_rooms: null, current_rooms: null, iroh_qad_port: 4605,
        uploaded_bytes: sequence * 6_250_000, forwarded_bytes: sequence * 12_500_000,
        bandwidth_capacity_mbps: null, forwarded_rate_bps: null,
        fresh: true,
    };
}

const mountedViews: VueWrapper[] = [];
let observedAtMs = 0;

function mountRelays(language: string): VueWrapper {
    const wrapper = mount(RelayManagement, {
        global: {
            plugins: [createI18n({ legacy: false, locale: language, messages: { en, zh } })],
            components: { "a-table": Table, "a-table-column": TableColumn },
            stubs: {
                "a-card": { template: "<section><slot name='extra'/><slot/></section>" },
                "a-button": { template: "<button><slot/></button>" },
                "a-tag": { template: "<span><slot/></span>" },
                "a-tooltip": { template: "<span><slot/></span>" },
                "a-alert": { props: ["message"], template: "<aside>{{ message }}</aside>" },
                "a-modal": { props: ["open"], template: "<article v-if='open'><slot/></article>" },
                "a-input": true, "a-input-number": true, "a-switch": true,
                "a-form": true, "a-form-item": true, "a-typography-paragraph": true,
            },
        },
    });
    mountedViews.push(wrapper);
    return wrapper;
}

function trafficCell(wrapper: VueWrapper): string {
    const columnIndex = wrapper.findAll("thead th")
        .findIndex(header => /forwarded Mbps|转发速率/.test(header.text()));
    return wrapper.findAll('tr[data-row-key="beijing-relay"] td')[columnIndex]!.text();
}

describe("Relay traffic presentation", () => {
    beforeEach(() => {
        vi.useFakeTimers({ toFake: ["setInterval", "clearInterval"] });
        vi.clearAllMocks();
        observedAtMs = 0;
        vi.spyOn(performance, "now").mockImplementation(() => observedAtMs);
        vi.stubGlobal("matchMedia", () => ({
            matches: false, addListener() {}, removeListener() {},
            addEventListener() {}, removeEventListener() {},
        }));
        vi.mocked(listManagedRelays).mockResolvedValue([relayReport(1)]);
    });
    afterEach(() => {
        mountedViews.splice(0).forEach(wrapper => wrapper.unmount());
        vi.restoreAllMocks();
        vi.unstubAllGlobals();
        vi.useRealTimers();
    });

    it.each(["zh", "en"])("shows unknown until the next report, then both rates in %s", async language => {
        const wrapper = mountRelays(language);
        await flushPromises();
        expect(trafficCell(wrapper)).toBe(language === "zh" ? "未知" : "Unknown");
        observedAtMs = 5_000;
        vi.mocked(listManagedRelays).mockResolvedValue([relayReport(2)]);
        await vi.mocked(useManagementRefresh).mock.calls[0]![1]();
        await flushPromises();
        expect(trafficCell(wrapper)).toBe("10.00 / 20.00");
        vi.mocked(listManagedRelays).mockResolvedValue([{ ...relayReport(3), generation: 4 }]);
        await vi.mocked(useManagementRefresh).mock.calls[0]![1]();
        await flushPromises();
        expect(trafficCell(wrapper)).toBe(language === "zh" ? "未知" : "Unknown");
    });

    it("hides stale rates even when repeated inventory reads still mark the relay fresh", async () => {
        const wrapper = mountRelays("en");
        await flushPromises();
        const refresh = vi.mocked(useManagementRefresh).mock.calls[0]![1];
        observedAtMs = 5_000;
        vi.mocked(listManagedRelays).mockResolvedValue([relayReport(2)]);
        await refresh();
        await flushPromises();
        expect(trafficCell(wrapper)).toBe("10.00 / 20.00");
        observedAtMs = 35_001;
        await refresh();
        await flushPromises();
        expect(trafficCell(wrapper)).toBe("Unknown");
    });

    it.each(["zh", "en"])("shows measured bandwidth headroom and unknown reset state in %s", async language => {
        const saturated = { ...relayReport(2), bandwidth_capacity_mbps: 20, forwarded_rate_bps: 18_000_000 };
        vi.mocked(listManagedRelays).mockResolvedValue([saturated]);
        const wrapper = mountRelays(language);
        await flushPromises();
        expect(wrapper.text()).toContain(language === "zh" ? "带宽余量不足" : "Insufficient bandwidth headroom");
        expect(wrapper.text()).toContain("20 Mbps");
        expect(wrapper.text()).toContain("18 Mbps / 90%");
        vi.mocked(listManagedRelays).mockResolvedValue([{ ...saturated, report_sequence: 3, forwarded_rate_bps: null }]);
        await vi.mocked(useManagementRefresh).mock.calls[0]![1]();
        await flushPromises();
        expect(wrapper.text()).toContain(language === "zh" ? "带宽负载待确认" : "Bandwidth load awaiting confirmation");
    });
});
