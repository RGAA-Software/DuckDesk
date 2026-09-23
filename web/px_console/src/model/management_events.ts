import { onBeforeUnmount, onMounted, reactive, readonly } from "vue";

import { getAdminToken } from "@/http.ts";

const CURSOR_KEY = "pixels.admin_web.management_cursor";
const RECONNECT_DELAY_MS = 2_000;

export type ManagementEventCategory =
    | "nodes"
    | "relays"
    | "devices"
    | "applications"
    | "instances"
    | "deployments"
    | "sessions"
    | "channels"
    | "file_transfers"
    | "recordings"
    | "guests"
    | "identities";

export interface ManagementEvent {
    type: "event";
    stream_id: string;
    sequence: number;
    occurred_at: string;
    category: ManagementEventCategory;
    resource_id: string | null;
}

export type ManagementRealtimeStatus = "disconnected" | "connecting" | "connected" | "stale";

const state = reactive({
    status: "disconnected" as ManagementRealtimeStatus,
    lastEventAt: "",
});

let activeConsumers = 0;
let reconnectTimer: number | undefined;
let socket: WebSocket | undefined;

interface Cursor {
    stream_id: string;
    after: number;
}

function readCursor(): Cursor | undefined {
    try {
        const value = JSON.parse(
            sessionStorage.getItem(CURSOR_KEY) ?? "null",
        ) as Partial<Cursor> | null;
        return value && typeof value.stream_id === "string" && Number.isSafeInteger(value.after)
            ? { stream_id: value.stream_id, after: value.after as number }
            : undefined;
    } catch {
        return undefined;
    }
}

function writeCursor(streamId: string, sequence: number): void {
    sessionStorage.setItem(CURSOR_KEY, JSON.stringify({ stream_id: streamId, after: sequence }));
}

function websocketUrl(): string {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    return `${protocol}//${window.location.host}/api/console/managed/events`;
}

function announceRefresh(): void {
    window.dispatchEvent(new CustomEvent("pixels:management-snapshot-required"));
}

function announceEvent(event: ManagementEvent): void {
    window.dispatchEvent(
        new CustomEvent<ManagementEvent>("pixels:management-event", { detail: event }),
    );
}

function scheduleReconnect(): void {
    if (activeConsumers === 0 || reconnectTimer !== undefined || !getAdminToken()) return;
    reconnectTimer = window.setTimeout(() => {
        reconnectTimer = undefined;
        connect();
    }, RECONNECT_DELAY_MS);
}

function connect(): void {
    if (activeConsumers === 0 || socket || !getAdminToken()) return;
    state.status = "connecting";
    const connection = new WebSocket(websocketUrl());
    socket = connection;
    connection.addEventListener("open", () => {
        const cursor = readCursor();
        connection.send(
            JSON.stringify({
                type: "authenticate",
                token: getAdminToken(),
                stream_id: cursor?.stream_id ?? null,
                after: cursor?.after ?? null,
            }),
        );
    });
    connection.addEventListener("message", rawMessage => {
        let message: Record<string, unknown>;
        try {
            message = JSON.parse(String(rawMessage.data)) as Record<string, unknown>;
        } catch {
            connection.close();
            return;
        }
        if (message.type === "ready") {
            state.status = "connected";
            if (message.snapshot_required === true) {
                writeCursor(String(message.stream_id), Number(message.latest_sequence));
                announceRefresh();
            }
            return;
        }
        if (message.type === "snapshot_required") {
            state.status = "stale";
            writeCursor(String(message.stream_id), Number(message.latest_sequence));
            announceRefresh();
            return;
        }
        if (message.type === "heartbeat") {
            state.status = "connected";
            return;
        }
        if (message.type === "event") {
            const event = message as unknown as ManagementEvent;
            writeCursor(event.stream_id, event.sequence);
            state.lastEventAt = event.occurred_at;
            state.status = "connected";
            announceEvent(event);
            return;
        }
        connection.close();
    });
    connection.addEventListener("close", () => {
        if (socket === connection) socket = undefined;
        state.status = activeConsumers > 0 ? "stale" : "disconnected";
        scheduleReconnect();
    });
    connection.addEventListener("error", () => connection.close());
}

function start(): void {
    activeConsumers += 1;
    connect();
}

function stop(): void {
    activeConsumers = Math.max(0, activeConsumers - 1);
    if (activeConsumers !== 0) return;
    if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
    reconnectTimer = undefined;
    socket?.close();
    socket = undefined;
    state.status = "disconnected";
}

export function useManagementEvents() {
    return readonly(state);
}

export function useManagementEventConnection(): void {
    onMounted(start);
    onBeforeUnmount(stop);
}

export function useManagementRefresh(
    categories: readonly ManagementEventCategory[],
    refresh: () => void | Promise<void>,
): void {
    const relevant = new Set(categories);
    const onEvent = (rawEvent: Event) => {
        const event = rawEvent as CustomEvent<ManagementEvent>;
        if (relevant.has(event.detail.category)) void refresh();
    };
    const onSnapshot = () => void refresh();
    onMounted(() => {
        window.addEventListener("pixels:management-event", onEvent);
        window.addEventListener("pixels:management-snapshot-required", onSnapshot);
    });
    onBeforeUnmount(() => {
        window.removeEventListener("pixels:management-event", onEvent);
        window.removeEventListener("pixels:management-snapshot-required", onSnapshot);
    });
}
