import { mount } from "@vue/test-utils";
import { defineComponent, h } from "vue";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { setAdminToken } from "@/http.ts";
import { useManagementEventConnection, useManagementEvents } from "@/model/management_events.ts";

class SyntheticWebSocket extends EventTarget {
    static instances: SyntheticWebSocket[] = [];

    readonly url: string;
    readonly sent: string[] = [];

    constructor(url: string | URL) {
        super();
        this.url = String(url);
        SyntheticWebSocket.instances.push(this);
    }

    send(value: string): void {
        this.sent.push(value);
    }

    close(): void {
        this.dispatchEvent(new Event("close"));
    }

    open(): void {
        this.dispatchEvent(new Event("open"));
    }

    receive(value: object): void {
        this.dispatchEvent(new MessageEvent("message", { data: JSON.stringify(value) }));
    }
}

const Consumer = defineComponent({
    setup() {
        useManagementEventConnection();
        const events = useManagementEvents();
        return () => h("span", events.status);
    },
});

beforeEach(() => {
    vi.useFakeTimers();
    sessionStorage.clear();
    SyntheticWebSocket.instances = [];
    vi.stubGlobal("WebSocket", SyntheticWebSocket);
    setAdminToken("a".repeat(64));
});

afterEach(() => {
    vi.runOnlyPendingTimers();
    vi.useRealTimers();
    vi.unstubAllGlobals();
    sessionStorage.clear();
});

describe("management event connection", () => {
    it("authenticates in the first message, persists its cursor and requests a snapshot", async () => {
        const snapshot = vi.fn();
        const event = vi.fn();
        window.addEventListener("pixels:management-snapshot-required", snapshot);
        window.addEventListener("pixels:management-event", event);
        const wrapper = mount(Consumer);
        const socket = SyntheticWebSocket.instances[0]!;

        expect(socket.url).toBe(
            "http://localhost:3000/api/console/managed/events".replace("http:", "ws:"),
        );
        expect(socket.url).not.toContain("token");
        socket.open();
        expect(JSON.parse(socket.sent[0]!)).toEqual({
            type: "authenticate",
            token: "a".repeat(64),
            stream_id: null,
            after: null,
        });

        socket.receive({
            type: "ready",
            stream_id: "stream-a",
            latest_sequence: 7,
            snapshot_required: true,
        });
        await wrapper.vm.$nextTick();
        expect(snapshot).toHaveBeenCalledOnce();
        expect(wrapper.text()).toBe("connected");

        socket.receive({
            type: "event",
            stream_id: "stream-a",
            sequence: 8,
            occurred_at: "2026-09-19T00:00:00Z",
            category: "nodes",
            resource_id: null,
        });
        expect(event).toHaveBeenCalledOnce();
        expect(sessionStorage.getItem("pixels.admin_web.management_cursor")).toBe(
            JSON.stringify({ stream_id: "stream-a", after: 8 }),
        );

        wrapper.unmount();
        window.removeEventListener("pixels:management-snapshot-required", snapshot);
        window.removeEventListener("pixels:management-event", event);
    });

    it("marks a broken connection stale and reconnects with the retained cursor", async () => {
        sessionStorage.setItem(
            "pixels.admin_web.management_cursor",
            JSON.stringify({ stream_id: "stream-b", after: 11 }),
        );
        const wrapper = mount(Consumer);
        const firstSocket = SyntheticWebSocket.instances[0]!;
        firstSocket.open();
        firstSocket.receive({
            type: "ready",
            stream_id: "stream-b",
            latest_sequence: 11,
            snapshot_required: false,
        });
        firstSocket.close();
        await wrapper.vm.$nextTick();
        expect(wrapper.text()).toBe("stale");

        vi.advanceTimersByTime(2_000);
        const secondSocket = SyntheticWebSocket.instances[1]!;
        secondSocket.open();
        expect(JSON.parse(secondSocket.sent[0]!)).toMatchObject({
            stream_id: "stream-b",
            after: 11,
        });
        wrapper.unmount();
    });
});
