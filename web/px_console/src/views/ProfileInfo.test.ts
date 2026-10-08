import { flushPromises, mount } from "@vue/test-utils";
import { Form, notification } from "ant-design-vue";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { changeAdminPassword, logoutAdmin, queryAdminSession } from "@/model/admin_session_api";
import ProfileInfo from "./ProfileInfo.vue";

const routerReplace = vi.hoisted(() => vi.fn());
vi.mock("vue-router", () => ({ useRouter: () => ({ replace: routerReplace }) }));
vi.mock("vue-i18n", () => ({ useI18n: () => ({ t: (key: string) => key }) }));
vi.mock("@/model/admin_session_api", () => ({
    changeAdminPassword: vi.fn(),
    logoutAdmin: vi.fn(),
    queryAdminSession: vi.fn(),
}));

function mountProfile() {
    return mount(ProfileInfo, {
        global: {
            components: { "a-form": Form },
            stubs: {
                "a-tabs": { template: "<div><slot /></div>" },
                "a-tab-pane": { template: "<section><slot /></section>" },
                "a-card": { template: "<section><slot /></section>" },
                "a-descriptions": { template: "<dl><slot /></dl>" },
                "a-descriptions-item": { template: "<div><slot /></div>" },
                "a-alert": true,
                "a-form-item": { template: "<div><slot /></div>" },
                "a-input-password": {
                    props: ["value"],
                    emits: ["update:value"],
                    template:
                        '<input :value="value" @input="$emit(\'update:value\', $event.target.value)" />',
                },
                "a-button": { template: "<button><slot /></button>" },
                LicenseStatusCard: true,
            },
        },
    });
}

describe("administrator password form submission", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.mocked(queryAdminSession).mockResolvedValue(null);
        vi.spyOn(notification, "error").mockImplementation(() => undefined);
        vi.spyOn(notification, "success").mockImplementation(() => undefined);
    });
    afterEach(() => vi.restoreAllMocks());

    it("submitting the real Ant form validates empty fields without calling the password API", async () => {
        const profile = mountProfile();
        try {
            await profile.get("form").trigger("submit");
            await flushPromises();
            expect(notification.error).toHaveBeenCalledWith({
                message: "adminProfile.passwordInvalid",
            });
            expect(changeAdminPassword).not.toHaveBeenCalled();
        } finally {
            profile.unmount();
        }
    });

    it("submitting valid matching fields changes the password then signs out", async () => {
        const profile = mountProfile();
        try {
            const passwordInputs = profile.findAll("input");
            await passwordInputs[0]!.setValue("existing-test-password");
            await passwordInputs[1]!.setValue("replacement-test-password");
            await passwordInputs[2]!.setValue("replacement-test-password");
            await profile.get("form").trigger("submit");
            await flushPromises();
            expect(changeAdminPassword).toHaveBeenCalledExactlyOnceWith(
                "existing-test-password",
                "replacement-test-password",
            );
            expect(logoutAdmin).toHaveBeenCalledOnce();
            expect(routerReplace).toHaveBeenCalledWith("/");
        } finally {
            profile.unmount();
        }
    });
});
