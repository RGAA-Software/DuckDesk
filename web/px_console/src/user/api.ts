import type { AxiosInstance } from "axios";
import { hasUserToken, publicHttp, setUserToken, userHttp, userResourceHttp } from "./http";

export interface UserProfile {
    id: string;
    username: string;
    role: "user" | "operator" | "administrator";
    authorization_revision: number;
    revision: number;
    avatar_url: string | null;
    created_at: string;
}

export interface DeviceSummary {
    device_id: string;
    public_code: string;
    name: string;
    platform: string;
    disabled: boolean;
    revision: number;
    registered_at: string;
}

export interface ApplicationCard {
    app_id: string;
    name: string;
    kind: "game_hook" | "webview" | "rdp";
    access_mode: "public" | "acl";
    revision: number;
    access_revision: number;
    running_instance?: Pick<
        InstanceView,
        "instance_id" | "state" | "revision" | "reconnectable" | "current_login_origin"
    >;
}

export interface InstanceView {
    instance_id: string;
    app_id: string;
    app_name: string;
    state: string;
    revision: number;
    created_at: string;
    stopped_at?: string;
    reconnectable: boolean;
    current_login_origin: boolean;
}

export interface ResourceSummary {
    device_count: number;
    application_count: number;
    active_instance_count: number;
}

export interface ResourcePage<T> {
    items: T[];
    page: number;
    page_size: number;
    total: number;
}

export interface RecordingView {
    id: string;
    node_id: string;
    session_id: string | null;
    file_name: string;
    size_bytes: number;
    modified_at: string;
    codec: string;
    reported_present: boolean;
    observed_at: string;
}

export interface RecordingCacheView {
    recording_id: string;
    state: "missing" | "fetching" | "ready" | "verifying" | "retry_required";
    size_bytes: number;
    received_bytes: number;
    updated_at: string;
}

interface DeviceRecord {
    id: string;
    public_code: string;
    name: string;
    platform: string;
    disabled: boolean;
    revision: number;
    registered_at: string;
}

interface ApplicationRecord {
    id: string;
    name: string;
    kind: ApplicationCard["kind"];
    access_mode: ApplicationCard["access_mode"];
    revision: number;
    access_revision: number;
}

interface InstanceRecord {
    id: string;
    application_id: string;
    client_type: string;
    state: string;
    revision: number;
    created_at: string;
    ended_at: string | null;
}

interface ResourceSession {
    id: string;
    target:
        | { kind: "desktop"; device_id: string }
        | { kind: "cloud_application"; application_id: string; instance_id: string };
    client_type: string;
    access_role: "controller" | "observer";
    state: string;
    revision: number;
    created_at: string;
    closed_at: string | null;
}

interface ResourceDescriptor {
    session: ResourceSession;
    host: string;
    port: number;
    transport: "native" | "rdp";
    expires_at: string;
}

interface DescriptorResponse {
    descriptor: ResourceDescriptor;
    token: string;
}

const ACTIVE_INSTANCE_STATES = new Set([
    "reserved",
    "starting",
    "running",
    "stopping",
    "reconcile_required",
]);
const INSTANCE_ORIGIN_PREFIX = "pixels.user.instance.";

function instanceStartedByCurrentLogin(instanceId: string) {
    return sessionStorage.getItem(`${INSTANCE_ORIGIN_PREFIX}${instanceId}`) === "1";
}

async function collectPages<T extends { id: string }>(
    http: AxiosInstance,
    path: string,
): Promise<T[]> {
    const result: T[] = [];
    let after: string | undefined;
    for (let page = 0; page < 100; page += 1) {
        const response = await http.get<T[]>(path, { params: { after, limit: 100 } });
        result.push(...response.data);
        if (response.data.length < 100) return result;
        after = response.data.at(-1)?.id;
    }
    throw new Error("resource directory exceeds the supported browser page window");
}

function pageOf<T>(items: T[], page: number, pageSize: number): ResourcePage<T> {
    const safePage = Math.max(1, page);
    const safePageSize = Math.max(1, pageSize);
    const offset = (safePage - 1) * safePageSize;
    return {
        items: items.slice(offset, offset + safePageSize),
        page: safePage,
        page_size: safePageSize,
        total: items.length,
    };
}

function mapDevice(record: DeviceRecord): DeviceSummary {
    return {
        device_id: record.id,
        public_code: record.public_code,
        name: record.name,
        platform: record.platform,
        disabled: record.disabled,
        revision: record.revision,
        registered_at: record.registered_at,
    };
}

function mapInstance(record: InstanceRecord, appNames: Map<string, string>): InstanceView {
    const currentLoginOrigin = instanceStartedByCurrentLogin(record.id);
    return {
        instance_id: record.id,
        app_id: record.application_id,
        app_name: appNames.get(record.application_id) ?? record.application_id,
        state: record.state,
        revision: record.revision,
        created_at: record.created_at,
        stopped_at: record.ended_at ?? undefined,
        reconnectable:
            record.state === "running" && record.client_type === "user_web" && currentLoginOrigin,
        current_login_origin: currentLoginOrigin,
    };
}

async function rawApplications(http: AxiosInstance, path: string) {
    return collectPages<ApplicationRecord>(http, path);
}

async function rawInstances(http: AxiosInstance) {
    return collectPages<InstanceRecord>(http, "/api/console/instances");
}

function requestId(storageKey: string) {
    const existing = sessionStorage.getItem(storageKey);
    if (existing) return existing;
    const created = crypto.randomUUID();
    sessionStorage.setItem(storageKey, created);
    return created;
}

function clearRequestId(storageKey: string) {
    sessionStorage.removeItem(storageKey);
}

export async function loginUser(username: string, password: string) {
    const response = await publicHttp.post<{ token: string; profile: UserProfile }>(
        "/api/console/sessions",
        { username, password },
    );
    setUserToken(response.data.token);
    return response.data.profile;
}

export async function queryUser(): Promise<UserProfile | null> {
    if (!hasUserToken()) return null;
    try {
        return (await userHttp.get<UserProfile>("/api/console/session")).data;
    } catch (error: any) {
        if (error?.response?.status === 401 || error?.response?.status === 403) {
            setUserToken("");
            return null;
        }
        throw error;
    }
}

export async function logoutUser() {
    try {
        await userHttp.delete("/api/console/session");
    } finally {
        setUserToken("");
    }
}

export async function getDevices() {
    return (await collectPages<DeviceRecord>(userHttp, "/api/console/devices")).map(mapDevice);
}

export async function getDevicesPage(page = 1, pageSize = 12, keyword = "") {
    const normalized = keyword.trim().toLocaleLowerCase();
    const devices = (await getDevices()).filter(
        device =>
            !normalized ||
            device.name.toLocaleLowerCase().includes(normalized) ||
            device.device_id.toLocaleLowerCase().includes(normalized) ||
            device.public_code.toLocaleLowerCase().includes(normalized),
    );
    return pageOf(devices, page, pageSize);
}

export async function getInstances() {
    const [instances, applications] = await Promise.all([
        rawInstances(userResourceHttp),
        rawApplications(userHttp, "/api/console/applications"),
    ]);
    const appNames = new Map(applications.map(application => [application.id, application.name]));
    return instances.map(instance => mapInstance(instance, appNames));
}

export async function getApps() {
    const [applications, instances] = await Promise.all([
        rawApplications(userHttp, "/api/console/applications"),
        rawInstances(userResourceHttp),
    ]);
    return applications.map<ApplicationCard>(application => {
        const running = instances.find(
            instance =>
                instance.application_id === application.id &&
                ACTIVE_INSTANCE_STATES.has(instance.state),
        );
        return {
            app_id: application.id,
            name: application.name,
            kind: application.kind,
            access_mode: application.access_mode,
            revision: application.revision,
            access_revision: application.access_revision,
            running_instance: running
                ? {
                      instance_id: running.id,
                      state: running.state,
                      revision: running.revision,
                      reconnectable:
                          running.state === "running" &&
                          running.client_type === "user_web" &&
                          instanceStartedByCurrentLogin(running.id),
                      current_login_origin: instanceStartedByCurrentLogin(running.id),
                  }
                : undefined,
        };
    });
}

export async function getAppsPage(page = 1, pageSize = 12, keyword = "") {
    const normalized = keyword.trim().toLocaleLowerCase();
    const applications = (await getApps()).filter(
        application =>
            !normalized ||
            application.name.toLocaleLowerCase().includes(normalized) ||
            application.app_id.toLocaleLowerCase().includes(normalized),
    );
    return pageOf(applications, page, pageSize);
}

export async function getInstancesPage(page = 1, pageSize = 10, keyword = "", state = "") {
    const normalized = keyword.trim().toLocaleLowerCase();
    const instances = (await getInstances()).filter(
        instance =>
            (!state || instance.state === state) &&
            (!normalized ||
                instance.app_name.toLocaleLowerCase().includes(normalized) ||
                instance.instance_id.toLocaleLowerCase().includes(normalized)),
    );
    return pageOf(instances, page, pageSize);
}

export async function getRecordingsPage(page = 1, pageSize = 10, keyword = "") {
    const normalized = keyword.trim().toLocaleLowerCase();
    const recordings = (
        await collectPages<RecordingView>(userResourceHttp, "/api/console/recordings")
    ).filter(
        recording =>
            !normalized ||
            recording.file_name.toLocaleLowerCase().includes(normalized) ||
            recording.session_id?.toLocaleLowerCase().includes(normalized),
    );
    return pageOf(recordings, page, pageSize);
}

export async function requestRecordingCache(recordingId: string) {
    return (
        await userResourceHttp.post<RecordingCacheView>(
            `/api/console/recordings/${encodeURIComponent(recordingId)}/cache`,
        )
    ).data;
}

export async function downloadRecording(recordingId: string) {
    return (
        await userResourceHttp.get<Blob>(
            `/api/console/recordings/${encodeURIComponent(recordingId)}/download`,
            { responseType: "blob" },
        )
    ).data;
}

export async function getSummary(): Promise<ResourceSummary> {
    const [devices, applications, instances] = await Promise.all([
        getDevices(),
        rawApplications(userHttp, "/api/console/applications"),
        rawInstances(userResourceHttp),
    ]);
    return {
        device_count: devices.length,
        application_count: applications.length,
        active_instance_count: instances.filter(instance =>
            ACTIVE_INSTANCE_STATES.has(instance.state),
        ).length,
    };
}

export async function waitForInstance(instanceId: string, timeoutMs = 30000) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        const record = (
            await userResourceHttp.get<InstanceRecord>(
                `/api/console/instances/${encodeURIComponent(instanceId)}`,
            )
        ).data;
        const instance = mapInstance(record, new Map());
        if (instance.state === "running") return instance;
        if (instance.state === "failed" || instance.state === "stopped") {
            throw new Error("实例启动失败");
        }
        await new Promise(resolve => window.setTimeout(resolve, 800));
    }
    throw new Error("实例启动超时");
}

function endpointHost(host: string) {
    return host.includes(":") && !host.startsWith("[") ? `[${host}]` : host;
}

export function prepareDescriptorLaunchUrl(response: DescriptorResponse) {
    const { descriptor, token } = response;
    const targetId =
        descriptor.session.target.kind === "desktop"
            ? descriptor.session.target.device_id
            : descriptor.session.target.instance_id;
    const launch = new URL(`http://${endpointHost(descriptor.host)}:${descriptor.port}/web/`);
    launch.searchParams.set("deviceId", targetId);
    launch.searchParams.set("stream_id", descriptor.session.id);
    if (descriptor.session.target.kind === "cloud_application") {
        launch.searchParams.set("instanceId", descriptor.session.target.instance_id);
    }
    const fragment = new URLSearchParams();
    fragment.set("session_id", descriptor.session.id);
    fragment.set("session_revision", String(descriptor.session.revision));
    fragment.set("frontend_token", token);
    fragment.set(
        "perms",
        descriptor.session.access_role === "controller"
            ? "view,input,clipboard,file,audio"
            : "view,audio",
    );
    launch.hash = fragment.toString();
    return launch.toString();
}

async function openTarget(
    http: AxiosInstance,
    target: ResourceSession["target"],
    access: ResourceSession["access_role"],
    requestKey: string,
) {
    const session = (
        await http.post<ResourceSession>("/api/console/resource-sessions", {
            request_id: requestId(requestKey),
            target,
            access,
        })
    ).data;
    const response = (
        await http.post<DescriptorResponse>(
            `/api/console/resource-sessions/${encodeURIComponent(session.id)}/descriptor`,
            { revision: session.revision },
        )
    ).data;
    clearRequestId(requestKey);
    window.location.assign(prepareDescriptorLaunchUrl(response));
}

export async function openDevice(deviceId: string, viewOnly = false) {
    const access = viewOnly ? "observer" : "controller";
    await openTarget(
        userResourceHttp,
        { kind: "desktop", device_id: deviceId },
        access,
        `pixels.user.open.desktop.${deviceId}.${access}`,
    );
}

export async function startApp(appId: string) {
    const storageKey = `pixels.user.start.${appId}`;
    const record = (
        await userResourceHttp.post<InstanceRecord>("/api/console/instances", {
            request_id: requestId(storageKey),
            application_id: appId,
            deployment_id: null,
        })
    ).data;
    sessionStorage.setItem(`${INSTANCE_ORIGIN_PREFIX}${record.id}`, "1");
    clearRequestId(storageKey);
    return { instance: mapInstance(record, new Map([[appId, appId]])) };
}

export async function openInstance(
    instance: InstanceView,
    _clientNonce?: string,
    viewOnly = false,
) {
    const access = viewOnly ? "observer" : "controller";
    await openTarget(
        userResourceHttp,
        {
            kind: "cloud_application",
            application_id: instance.app_id,
            instance_id: instance.instance_id,
        },
        access,
        `pixels.user.open.application.${instance.instance_id}.${access}`,
    );
}

export async function stopInstance(instanceId: string) {
    const current = (
        await userResourceHttp.get<InstanceRecord>(
            `/api/console/instances/${encodeURIComponent(instanceId)}`,
        )
    ).data;
    const stopped = (
        await userResourceHttp.post<InstanceRecord>(
            `/api/console/instances/${encodeURIComponent(instanceId)}/stop`,
            { revision: current.revision },
        )
    ).data;
    if (stopped.ended_at) sessionStorage.removeItem(`${INSTANCE_ORIGIN_PREFIX}${instanceId}`);
    return mapInstance(stopped, new Map());
}

export async function updateUserName(username: string, revision: number) {
    return (await userHttp.patch<UserProfile>("/api/console/profile", { username, revision })).data;
}

export async function uploadUserAvatar(file: File, revision: number) {
    return (
        await userHttp.put<UserProfile>("/api/console/profile/avatar", file, {
            params: { revision },
            headers: { "Content-Type": file.type },
        })
    ).data;
}

export async function changeUserPassword(currentPassword: string, newPassword: string) {
    await userHttp.patch("/api/console/password", {
        current_password: currentPassword,
        new_password: newPassword,
    });
    setUserToken("");
}
