import { flushPromises, mount } from "@vue/test-utils";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { message } from "ant-design-vue";
import {
    createManagedApplication,
    replaceManagedApplicationGroups,
    updateManagedApplication,
} from "@/model/managed_application_api";
import ApplicationCatalogCard from "./ApplicationCatalogCard.vue";

vi.mock("@/model/management_events", () => ({ useManagementRefresh: vi.fn() }));
vi.mock("@/model/identity_api", () => ({ listGroups: vi.fn().mockResolvedValue([]) }));
vi.mock("@/model/managed_application_api", () => ({
    listManagedApplications: vi.fn().mockResolvedValue([]),
    createManagedApplication: vi.fn(),
    replaceManagedApplicationGroups: vi.fn(),
    updateManagedApplication: vi.fn(),
    deleteManagedApplication: vi.fn(),
    getManagedApplicationGroups: vi.fn(),
}));
vi.mock("ant-design-vue", () => ({
    message: { error: vi.fn(), success: vi.fn() },
    Modal: { confirm: vi.fn() },
}));
vi.mock("vue-i18n", () => ({ useI18n: () => ({ t: (key: string) => key }) }));

function mountCatalog() {
    return mount(ApplicationCatalogCard, {
        global: {
            stubs: {
                "a-card": { template: "<section><slot name='extra' /><slot /></section>" },
                "a-button": { template: "<button><slot /></button>" },
                "a-table": true,
                "a-table-column": true,
                "a-tag": true,
                "a-space": true,
                "a-modal": {
                    props: ["open"],
                    emits: ["ok"],
                    template:
                        "<section v-if='open'><slot/><button class='save' @click='$emit(\"ok\")'>Save</button></section>",
                },
                "a-form": { template: "<form><slot/></form>" },
                "a-form-item": { template: "<label><slot/></label>" },
                "a-input": {
                    props: ["value"],
                    emits: ["update:value"],
                    template:
                        "<input :value='value' @input='$emit(\"update:value\", $event.target.value)'/>",
                },
                "a-textarea": true,
                "a-select": true,
                "a-input-number": true,
                "a-row": { template: "<div><slot/></div>" },
                "a-col": { template: "<div><slot/></div>" },
                "a-switch": true,
                "a-radio-group": true,
                "a-radio": true,
            },
        },
    });
}

describe("application catalog save", () => {
    beforeEach(() => vi.clearAllMocks());

    it("keeps an invalid full path in the editor and explains it without sending a request", async () => {
        const catalog = mountCatalog();
        await flushPromises();
        await catalog.find("button").trigger("click");
        await catalog.findAll("input")[0]!.setValue("2D Adventure");
        await catalog.findAll("input")[1]!.setValue("D:\\software\\2dadventure\\2dAdventure.exe");
        await catalog.find(".save").trigger("click");
        await flushPromises();
        expect(createManagedApplication).not.toHaveBeenCalled();
        expect(message.error).toHaveBeenCalledWith("applications.validation.executable");
        expect(catalog.findAll("input")[1]!.element.value).toBe(
            "D:\\software\\2dadventure\\2dAdventure.exe",
        );
        catalog.unmount();
    });

    it("displays server rejection and retains the form for correction", async () => {
        vi.mocked(createManagedApplication).mockRejectedValueOnce({
            isAxiosError: true,
            response: { status: 400 },
        });
        const catalog = mountCatalog();
        await flushPromises();
        await catalog.find("button").trigger("click");
        await catalog.findAll("input")[0]!.setValue("2D Adventure");
        await catalog.findAll("input")[1]!.setValue("2dAdventure.exe");
        await catalog.find(".save").trigger("click");
        await flushPromises();
        expect(createManagedApplication).toHaveBeenCalledOnce();
        expect(message.error).toHaveBeenCalledWith("applications.messages.invalidInput");
        expect(catalog.find(".save").exists()).toBe(true);
        catalog.unmount();
    });

    it("retries a failed group save using the created application instead of creating a duplicate", async () => {
        vi.mocked(createManagedApplication).mockImplementationOnce(async spec => ({
            id: "created-application",
            revision: 1,
            access_revision: 1,
            spec,
        }));
        vi.mocked(replaceManagedApplicationGroups)
            .mockRejectedValueOnce(new Error("network unavailable"))
            .mockImplementationOnce(async application => ({ ...application, revision: 3 }));
        vi.mocked(updateManagedApplication).mockImplementationOnce(async (application, spec) => ({
            ...application,
            revision: 2,
            spec,
        }));
        const catalog = mountCatalog();
        await flushPromises();
        await catalog.find("button").trigger("click");
        await catalog.findAll("input")[0]!.setValue("2D Adventure");
        await catalog.findAll("input")[1]!.setValue("2dAdventure.exe");
        await catalog.find(".save").trigger("click");
        await flushPromises();
        expect(message.error).toHaveBeenCalledWith("applications.messages.saveFailed");
        await catalog.find(".save").trigger("click");
        await flushPromises();
        expect(createManagedApplication).toHaveBeenCalledOnce();
        expect(updateManagedApplication).toHaveBeenCalledWith(
            expect.objectContaining({ id: "created-application", revision: 1 }),
            expect.anything(),
        );
        expect(message.success).toHaveBeenCalledWith("applications.messages.saved");
        expect(catalog.find(".save").exists()).toBe(false);
        catalog.unmount();
    });
});
