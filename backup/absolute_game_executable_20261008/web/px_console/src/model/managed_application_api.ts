import axiosHttp from "@/http";

export type ApplicationAccess = "public" | "acl";
export type VideoCodec = "h264" | "h265";

export interface VideoSpec {
    codec: VideoCodec;
    bitrate_kbps: number;
}

export type ApplicationLaunch =
    | { kind: "game_hook"; executable_relative: string; arguments: string; video: VideoSpec }
    | { kind: "webview"; entry_url: string; video: VideoSpec }
    | { kind: "rdp" };

export interface ApplicationSpec {
    name: string;
    access: ApplicationAccess;
    launch: ApplicationLaunch;
    allow_observer: boolean;
    allow_takeover: boolean;
    disabled: boolean;
}

export interface ManagedApplication {
    id: string;
    revision: number;
    access_revision: number;
    spec: ApplicationSpec;
}

export async function listManagedApplications(): Promise<ManagedApplication[]> {
    const applications: ManagedApplication[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedApplication[]>(
            "/api/console/managed/applications",
            {
                params: { after, limit: 100 },
            },
        );
        applications.push(...response.data);
        if (response.data.length < 100) return applications;
        after = response.data.at(-1)?.id;
        if (!after)
            throw new Error("A managed application page did not include its cursor identity");
    }
}

export async function createManagedApplication(spec: ApplicationSpec): Promise<ManagedApplication> {
    const response = await axiosHttp.post<ManagedApplication>(
        "/api/console/managed/applications",
        spec,
    );
    return response.data;
}

export async function updateManagedApplication(
    application: ManagedApplication,
    spec: ApplicationSpec,
): Promise<ManagedApplication> {
    const response = await axiosHttp.patch<ManagedApplication>(
        `/api/console/managed/applications/${encodeURIComponent(application.id)}`,
        { revision: application.revision, spec },
    );
    return response.data;
}

export async function deleteManagedApplication(application: ManagedApplication): Promise<void> {
    await axiosHttp.delete(
        `/api/console/managed/applications/${encodeURIComponent(application.id)}`,
        {
            params: { revision: application.revision },
        },
    );
}

export async function getManagedApplicationGroups(
    application: ManagedApplication,
): Promise<string[]> {
    const response = await axiosHttp.get<string[]>(
        `/api/console/managed/applications/${encodeURIComponent(application.id)}/groups`,
    );
    return response.data;
}

export async function replaceManagedApplicationGroups(
    application: ManagedApplication,
    groups: string[],
): Promise<ManagedApplication> {
    const response = await axiosHttp.put<ManagedApplication>(
        `/api/console/managed/applications/${encodeURIComponent(application.id)}/groups`,
        { revision: application.revision, groups },
    );
    return response.data;
}
