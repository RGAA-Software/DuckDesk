import { prepareDescriptorLaunchUrl, type ApplicationCard, type InstanceView } from "./api";
import { guestHttp, guestResourceHttp, hasGuestToken, publicHttp, setGuestToken } from "./http";

interface GuestApplicationRecord {
    id: string;
    name: string;
    kind: ApplicationCard["kind"];
    access_mode: ApplicationCard["access_mode"];
    revision: number;
    access_revision: number;
}

interface GuestInstanceRecord {
    id: string;
    application_id: string;
    state: string;
    revision: number;
    created_at: string;
    ended_at: string | null;
}

interface GuestResourceSession {
    id: string;
    target: { kind: "cloud_application"; application_id: string; instance_id: string };
    access_role: "controller" | "observer";
    revision: number;
}

function requestId(storageKey: string) {
    const existing = sessionStorage.getItem(storageKey);
    if (existing) return existing;
    const created = crypto.randomUUID();
    sessionStorage.setItem(storageKey, created);
    return created;
}

async function guestRequest<T>(request: () => Promise<T>): Promise<T> {
    await ensureGuestSession();
    try {
        return await request();
    } catch (error: any) {
        if (error?.response?.status !== 401 && error?.response?.status !== 403) throw error;
        await ensureGuestSession(true);
        return request();
    }
}

async function collectGuestPages<T extends { id: string }>(path: string): Promise<T[]> {
    const result: T[] = [];
    let after: string | undefined;
    for (let page = 0; page < 100; page += 1) {
        const response = await guestRequest(() =>
            guestHttp.get<T[]>(path, {
                params: { after, limit: 100 },
            }),
        );
        result.push(...response.data);
        if (response.data.length < 100) return result;
        after = response.data.at(-1)?.id;
    }
    throw new Error("public application directory exceeds the supported browser page window");
}

function mapInstance(record: GuestInstanceRecord, appName = record.application_id): InstanceView {
    return {
        instance_id: record.id,
        app_id: record.application_id,
        app_name: appName,
        state: record.state,
        revision: record.revision,
        created_at: record.created_at,
        stopped_at: record.ended_at ?? undefined,
        reconnectable: record.state === "running",
        current_login_origin: true,
    };
}

export async function ensureGuestSession(force = false) {
    if (!force && hasGuestToken()) return;
    if (force) setGuestToken("");
    const response = await guestHttp.post<{ token: string }>("/api/console/guest-sessions", {});
    setGuestToken(response.data.token);
}

export async function registerUser(username: string, password: string) {
    return (
        await publicHttp.post<{ id: string; username: string }>("/api/console/accounts", {
            username,
            password,
        })
    ).data;
}

export async function getPublicApps(): Promise<ApplicationCard[]> {
    const applications = await collectGuestPages<GuestApplicationRecord>(
        "/api/console/guest/applications",
    );
    return applications.map(application => ({
        app_id: application.id,
        name: application.name,
        kind: application.kind,
        access_mode: application.access_mode,
        revision: application.revision,
        access_revision: application.access_revision,
    }));
}

export async function startPublicApp(appId: string) {
    const storageKey = `pixels.guest.start.${appId}`;
    const response = await guestRequest(() =>
        guestResourceHttp.post<GuestInstanceRecord>("/api/console/instances", {
            request_id: requestId(storageKey),
            application_id: appId,
            deployment_id: null,
        }),
    );
    sessionStorage.removeItem(storageKey);
    return { instance: mapInstance(response.data) };
}

export async function waitForGuestInstance(instanceId: string, timeoutMs = 30000) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        const response = await guestRequest(() =>
            guestResourceHttp.get<GuestInstanceRecord>(
                `/api/console/instances/${encodeURIComponent(instanceId)}`,
            ),
        );
        const instance = mapInstance(response.data);
        if (instance.state === "running") return instance;
        if (instance.state === "failed" || instance.state === "stopped") {
            throw new Error("实例启动失败");
        }
        await new Promise(resolve => window.setTimeout(resolve, 800));
    }
    throw new Error("实例启动超时");
}

export async function getGuestInstances() {
    const records = await guestRequest(async () => {
        const result: GuestInstanceRecord[] = [];
        let after: string | undefined;
        for (let page = 0; page < 100; page += 1) {
            const response = await guestResourceHttp.get<GuestInstanceRecord[]>(
                "/api/console/instances",
                { params: { after, limit: 100 } },
            );
            result.push(...response.data);
            if (response.data.length < 100) return result;
            after = response.data.at(-1)?.id;
        }
        throw new Error("guest instance directory exceeds the supported browser page window");
    });
    const applications = await getPublicApps();
    const appNames = new Map(
        applications.map(application => [application.app_id, application.name]),
    );
    return records.map(record => mapInstance(record, appNames.get(record.application_id)));
}

export async function openGuestInstance(
    instance: InstanceView,
    _clientNonce: string,
    viewOnly = false,
) {
    const access = viewOnly ? "observer" : "controller";
    const storageKey = `pixels.guest.open.${instance.instance_id}.${access}`;
    const sessionResponse = await guestRequest(() =>
        guestResourceHttp.post<GuestResourceSession>("/api/console/resource-sessions", {
            request_id: requestId(storageKey),
            target: {
                kind: "cloud_application",
                application_id: instance.app_id,
                instance_id: instance.instance_id,
            },
            access,
        }),
    );
    const session = sessionResponse.data;
    const descriptorResponse = await guestRequest(() =>
        guestResourceHttp.post<Parameters<typeof prepareDescriptorLaunchUrl>[0]>(
            `/api/console/resource-sessions/${encodeURIComponent(session.id)}/descriptor`,
            { revision: session.revision },
        ),
    );
    sessionStorage.removeItem(storageKey);
    window.location.assign(prepareDescriptorLaunchUrl(descriptorResponse.data));
}

export async function stopGuestInstance(instanceId: string) {
    const current = await guestRequest(() =>
        guestResourceHttp.get<GuestInstanceRecord>(
            `/api/console/instances/${encodeURIComponent(instanceId)}`,
        ),
    );
    const stopped = await guestRequest(() =>
        guestResourceHttp.post<GuestInstanceRecord>(
            `/api/console/instances/${encodeURIComponent(instanceId)}/stop`,
            { revision: current.data.revision },
        ),
    );
    return mapInstance(stopped.data);
}
