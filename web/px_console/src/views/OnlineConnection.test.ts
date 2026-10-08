import { flushPromises, mount, type VueWrapper } from "@vue/test-utils";
import { Table } from "ant-design-vue";
import { createI18n } from "vue-i18n";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { listManagedResourceSessions, type ResourceSession } from "@/model/managed_activity_api";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import { listAllAdminUsers, type UserAdminView } from "@/model/identity_api";
import zh from "@/locales/zh";
import en from "@/locales/en";
import OnlineConnection from "./OnlineConnection.vue";

vi.mock("@/model/managed_activity_api", () => ({ listManagedResourceSessions: vi.fn() }));
vi.mock("@/model/managed_application_api", () => ({ listManagedApplications: vi.fn() }));
vi.mock("@/model/managed_device_api", () => ({ listManagedDevices: vi.fn() }));
vi.mock("@/model/identity_api", () => ({ listAllAdminUsers: vi.fn() }));
vi.mock("@/model/management_events", () => ({ useManagementRefresh: vi.fn() }));

const application: ManagedApplication = {
    id: "application",
    revision: 1,
    access_revision: 1,
    spec: {
        name: "Adventure",
        access: "public",
        launch: {
            kind: "game_hook",
            executable_path: "D:\\Games\\Adventure.exe",
            arguments: "--windowed",
            video: { codec: "h264", bitrate_kbps: 8000 },
        },
        disabled: false,
        disconnect_grace_seconds: 10,
        allow_observer: true,
        allow_takeover: false,
    },
};
const session: ResourceSession = {
    id: "guest-session",
    target: { kind: "cloud_application", application_id: application.id, instance_id: "instance" },
    owner: { kind: "guest", guest_id: "guest" },
    client_type: "panel",
    access_role: "controller",
    state: "connected",
    revision: 1,
    created_at: "2026-10-08T08:22:20Z",
    closed_at: null,
};
const mounted: VueWrapper[] = [];
function mountConnections(language = "zh") {
    const wrapper = mount(OnlineConnection, {
        global: {
            plugins: [createI18n({ legacy: false, locale: language, messages: { zh, en } })],
            components: { "a-table": Table },
            stubs: {
                "a-card": { template: "<section><slot name='extra'/><slot/></section>" },
                "a-button": { template: "<button><slot/></button>" },
                "a-tag": { template: "<span><slot/></span>" },
                "a-alert": { props: ["message"], template: "<aside>{{ message }}</aside>" },
                "a-modal": { props: ["open"], template: "<article v-if='open'><slot/></article>" },
                "a-descriptions": { template: "<dl><slot/></dl>" },
                "a-descriptions-item": { template: "<dd><slot/></dd>" },
            },
        },
    });
    mounted.push(wrapper);
    return wrapper;
}
describe("online connection columns", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.stubGlobal("matchMedia", () => ({
            matches: false,
            addListener() {},
            removeListener() {},
            addEventListener() {},
            removeEventListener() {},
        }));
        vi.mocked(listManagedApplications).mockResolvedValue([application]);
        vi.mocked(listManagedDevices).mockResolvedValue([
            { id: "device", name: "Workstation" },
        ] as ManagedDevice[]);
        vi.mocked(listAllAdminUsers).mockResolvedValue([
            { uid: "user", username: "Alice" },
        ] as UserAdminView[]);
        vi.mocked(listManagedResourceSessions).mockResolvedValue([session]);
    });
    afterEach(() => {
        mounted.splice(0).forEach(wrapper => wrapper.unmount());
        vi.unstubAllGlobals();
    });
    it("renders all seven guest cells under their correct headers and opens application details", async () => {
        const wrapper = mountConnections();
        await flushPromises();
        expect(wrapper.findAll("thead th").map(header => header.text())).toEqual([
            "会话 ID",
            "所有者",
            "目标",
            "客户端",
            "访问角色",
            "状态",
            "创建时间",
        ]);
        const cells = wrapper.findAll('tr[data-row-key="guest-session"] td');
        expect(cells).toHaveLength(7);
        expect(cells.map(cell => cell.text())).toEqual([
            session.id,
            "访客",
            "云应用Adventure",
            "Windows 客户端",
            "控制",
            "已连接",
            new Intl.DateTimeFormat("zh", {
                dateStyle: "medium",
                timeStyle: "medium",
                hour12: false,
            }).format(new Date(session.created_at)),
        ]);
        await wrapper.get(".application-target").trigger("click");
        expect(wrapper.get("article").text()).toContain("D:\\Games\\Adventure.exe");
        expect(wrapper.get("article").text()).toContain("--windowed");
        expect(wrapper.text()).not.toContain("包含已关闭");
    });
    it("maps user identity, desktop name, viewer role and pending state in English", async () => {
        vi.mocked(listManagedResourceSessions).mockResolvedValue([
            {
                ...session,
                owner: { kind: "user", user_id: "user" },
                target: { kind: "desktop", device_id: "device" },
                client_type: "android",
                access_role: "observer",
                state: "pending",
            },
        ]);
        const wrapper = mountConnections("en");
        await flushPromises();
        const cells = wrapper
            .findAll('tr[data-row-key="guest-session"] td')
            .map(cell => cell.text());
        expect(cells.slice(1, 6)).toEqual([
            "Alice",
            "DesktopWorkstation",
            "Android client",
            "View only",
            "Connecting",
        ]);
    });
    it("excludes closed sessions and keeps unknown application cells in place without exposing its ID", async () => {
        vi.mocked(listManagedApplications).mockResolvedValue([]);
        vi.mocked(listManagedResourceSessions).mockResolvedValue([
            session,
            { ...session, id: "closed-state", state: "closed" },
            { ...session, id: "closed-time", closed_at: session.created_at },
        ]);
        const wrapper = mountConnections();
        await flushPromises();
        expect(wrapper.findAll("tr[data-row-key]")).toHaveLength(1);
        const cells = wrapper.findAll('tr[data-row-key="guest-session"] td');
        expect(cells).toHaveLength(7);
        expect(cells[2]!.text()).toBe("云应用应用已删除或不可用");
        expect(wrapper.find(".application-target").exists()).toBe(false);
    });
    it("reports refresh failure while preserving the previous result", async () => {
        const wrapper = mountConnections();
        await flushPromises();
        vi.mocked(listManagedResourceSessions).mockRejectedValueOnce(
            new Error("network unavailable"),
        );
        await wrapper.find("button").trigger("click");
        await flushPromises();
        expect(wrapper.find("aside").text()).toContain("刷新连接信息失败");
        expect(wrapper.findAll('tr[data-row-key="guest-session"] td')).toHaveLength(7);
    });
});
