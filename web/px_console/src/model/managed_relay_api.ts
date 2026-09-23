import axiosHttp from "@/http";

export interface ManagedRelay {
    id: string;
    name: string;
    public_host: string;
    public_port: number;
    revision: number;
    generation: number;
    control_epoch: number | null;
    state: string;
    desired_draining: boolean;
    reported_draining: boolean | null;
    disabled: boolean;
    report_sequence: number;
    last_seen: string | null;
    product_version_code: number | null;
    max_connections: number | null;
    current_connections: number | null;
    max_rooms: number | null;
    current_rooms: number | null;
    uploaded_bytes: number | null;
    forwarded_bytes: number | null;
    fresh: boolean;
}

interface RelayCredentialResponse {
    relay: ManagedRelay;
    relay_token: string;
}

export async function listManagedRelays(): Promise<ManagedRelay[]> {
    const relays: ManagedRelay[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedRelay[]>("/api/console/managed/relays", {
            params: { after, limit: 100 },
        });
        relays.push(...response.data);
        if (response.data.length < 100) return relays;
        after = response.data.at(-1)?.id;
        if (!after) throw new Error("A managed Relay page did not include its cursor identity");
    }
}

export async function createManagedRelay(
    name: string,
    publicHost: string,
    publicPort: number,
): Promise<RelayCredentialResponse> {
    const response = await axiosHttp.post<RelayCredentialResponse>("/api/console/managed/relays", {
        name,
        public_host: publicHost,
        public_port: publicPort,
    });
    return response.data;
}

export async function configureManagedRelay(
    relay: ManagedRelay,
    configuration: { draining: boolean; disabled: boolean },
): Promise<ManagedRelay> {
    const response = await axiosHttp.patch<ManagedRelay>(
        `/api/console/managed/relays/${encodeURIComponent(relay.id)}`,
        {
            revision: relay.revision,
            configuration,
        },
    );
    return response.data;
}
