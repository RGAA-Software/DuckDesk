import axiosHttp from "@/http";

export type ResourceOwner = { user: { user_id: string } } | { guest: { guest_id: string } };
export type SessionTarget =
    | { kind: "desktop"; device_id: string }
    | { kind: "cloud_application"; application_id: string; instance_id: string };

export interface ResourceSession {
    id: string;
    target: SessionTarget;
    owner: ResourceOwner;
    client_type: string;
    access_role: string;
    state: string;
    revision: number;
    created_at: string;
    closed_at: string | null;
}

export interface VisitRecord {
    session: ResourceSession;
    node_id: string;
    first_connected_at: string | null;
    channel_count: number;
}

export interface ChannelRecord {
    id: string;
    session_id: string;
    node_id: string;
    kind: string;
    state: string;
    reason: string | null;
    sequence: number;
    revision: number;
    sent_bytes: number;
    received_bytes: number;
    elapsed_ms: number;
    created_at: string;
    updated_at: string;
    ended_at: string | null;
}

export interface FileTransferRecord {
    id: string;
    session_id: string;
    node_id: string;
    direction: string;
    file_name: string;
    total_bytes: number;
    transferred_bytes: number;
    state: string;
    reason: string | null;
    sequence: number;
    revision: number;
    created_at: string;
    updated_at: string;
    ended_at: string | null;
}

export interface RecordingProfile {
    id: string;
    node_id: string;
    session_id: string | null;
    file_name: string;
    size_bytes: number;
    modified_at: string;
    codec: string;
    reported_present: boolean;
    source_sequence: number;
    revision: number;
    created_at: string;
    observed_at: string;
}

async function collectPages<T extends { id: string }>(
    path: string,
    extra: Record<string, string | undefined> = {},
): Promise<T[]> {
    const records: T[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<T[]>(path, {
            params: { ...extra, after, limit: 100 },
        });
        records.push(...response.data);
        if (response.data.length < 100) return records;
        after = response.data.at(-1)?.id;
        if (!after) throw new Error("A managed activity page did not include its cursor identity");
    }
}

export function listManagedResourceSessions(): Promise<ResourceSession[]> {
    return collectPages<ResourceSession>("/api/console/managed/resource-sessions");
}

export async function listManagedVisits(): Promise<VisitRecord[]> {
    const visits: VisitRecord[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<VisitRecord[]>(
            "/api/console/managed/activity/visits",
            {
                params: { after, limit: 100 },
            },
        );
        visits.push(...response.data);
        if (response.data.length < 100) return visits;
        after = response.data.at(-1)?.session.id;
        if (!after)
            throw new Error("A managed visit page did not include its session cursor identity");
    }
}

export function listManagedChannels(session?: string): Promise<ChannelRecord[]> {
    return collectPages<ChannelRecord>("/api/console/managed/activity/channels", { session });
}

export function listManagedFileTransfers(node?: string): Promise<FileTransferRecord[]> {
    return collectPages<FileTransferRecord>("/api/console/managed/file-transfers", { node });
}

export function listManagedRecordings(node?: string): Promise<RecordingProfile[]> {
    return collectPages<RecordingProfile>("/api/console/managed/recordings", { node });
}
