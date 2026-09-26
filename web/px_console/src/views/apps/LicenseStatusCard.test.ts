import { flushPromises, mount } from "@vue/test-utils";
import { ref } from "vue";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { getManagedLicenseStatus, installManagedLicense } from "@/model/managed_license_api";
import LicenseStatusCard from "./LicenseStatusCard.vue";

vi.mock("@/model/managed_license_api", () => ({
    getManagedLicenseStatus: vi.fn(),
    installManagedLicense: vi.fn(),
}));

vi.mock("vue-i18n", () => ({
    useI18n: () => ({ locale: ref("en-US"), t: (key: string) => key }),
}));

function mountLicenseCard() {
    return mount(LicenseStatusCard, {
        global: {
            stubs: {
                "a-card": { template: "<section><slot name='extra' /><slot /></section>" },
                "a-button": { template: "<button><slot /></button>" },
                "a-alert": { props: ["message"], template: "<div>{{ message }}</div>" },
                "a-descriptions": { template: "<dl><slot /></dl>" },
                "a-descriptions-item": { template: "<div><slot /></div>" },
            },
        },
    });
}

describe("license status card", () => {
    beforeEach(() => vi.clearAllMocks());

    it("shows the verified server entitlement without inventing a live stream count", async () => {
        vi.mocked(getManagedLicenseStatus).mockResolvedValue({
            license_id: "00000000-0000-0000-0000-000000000001",
            revision: 3,
            expires_at: 1790294400,
            max_streams: 4,
            services: ["cloud_applications", "rdp"],
        });

        const wrapper = mountLicenseCard();
        await flushPromises();

        expect(wrapper.text()).toContain("dashboard.licenseServices.cloudApplications");
        expect(wrapper.text()).toContain("dashboard.licenseServices.rdp");
        expect(wrapper.text()).toContain("4");
        expect(wrapper.text()).toContain("00000000-0000-0000-0000-000000000001");
        wrapper.unmount();
    });

    it("clears stale license data when the administrator endpoint is unavailable", async () => {
        vi.mocked(getManagedLicenseStatus)
            .mockResolvedValueOnce({
                license_id: "00000000-0000-0000-0000-000000000001",
                revision: 3,
                expires_at: 1790294400,
                max_streams: 4,
                services: ["desktop"],
            })
            .mockRejectedValueOnce(new Error("unavailable"));

        const wrapper = mountLicenseCard();
        await flushPromises();
        await wrapper.find("button").trigger("click");
        await flushPromises();

        expect(wrapper.text()).toContain("dashboard.licenseUnavailable");
        expect(wrapper.text()).not.toContain("00000000-0000-0000-0000-000000000001");
        wrapper.unmount();
    });

    it("shows the authorization action while no license is installed", async () => {
        vi.mocked(getManagedLicenseStatus).mockResolvedValue(null);
        const wrapper = mountLicenseCard();
        await flushPromises();

        expect(wrapper.text()).toContain("dashboard.licenseNotActivated");
        expect(wrapper.find('input[type="file"]').exists()).toBe(true);
        expect(installManagedLicense).not.toHaveBeenCalled();
        wrapper.unmount();
    });
});
