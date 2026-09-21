import axiosHttp from "@/http";

export type UpdateProduct = "cloud_node" | "client" | "remote" | "android" | "server";

export interface ManagedUpdateRelease {
    id: string;
    artifact: {
        target: {
            product: UpdateProduct;
            distribution: "official" | "customer" | "oem";
            release_namespace: string;
            oem_id: string | null;
            channel: "stable" | "preview";
            os: "windows" | "linux" | "android";
            architecture: "x86_64" | "aarch64";
        };
        build_number: number;
        version: string;
    };
    repository_publication_sha256: string;
    repository_root_version: number;
    state: string;
    revision: number;
    created_at: string;
    updated_at: string;
}

export interface NodeUpdateTrustSummary {
    release_id: string;
    repository_publication_sha256: string;
    required_root_version: number;
    eligible_node_count: number;
    confirmed_node_count: number;
    unknown_or_behind_node_count: number;
    minimum_confirmed_root_version: number | null;
    oldest_confirmation_at: string | null;
}

export interface NodeUpdateTrustStatus {
    node_id: string;
    device_id: string;
    node_state: string;
    disabled: boolean;
    last_seen: string | null;
    observed_release_id: string | null;
    repository_publication_sha256: string | null;
    trusted_root_version: number | null;
    observed_at: string | null;
    confirmed: boolean;
}

export function supportsNodeTrust(release: ManagedUpdateRelease): boolean {
    return (
        release.artifact.target.product === "cloud_node" ||
        release.artifact.target.product === "remote"
    );
}

export async function listManagedUpdates(): Promise<ManagedUpdateRelease[]> {
    const releases: ManagedUpdateRelease[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedUpdateRelease[]>(
            "/api/console/managed/updates",
            {
                params: { after, limit: 100 },
            },
        );
        releases.push(...response.data);
        if (response.data.length < 100) return releases;
        const nextCursor = response.data.at(-1)?.id;
        if (!nextCursor || nextCursor === after) {
            throw new Error("A managed update page did not advance its cursor identity");
        }
        after = nextCursor;
    }
}

export async function getNodeUpdateTrustSummary(
    releaseId: string,
): Promise<NodeUpdateTrustSummary> {
    const encodedReleaseId = encodeURIComponent(releaseId);
    const response = await axiosHttp.get<NodeUpdateTrustSummary>(
        `/api/console/managed/updates/${encodedReleaseId}/node-trust`,
    );
    return response.data;
}

export async function listNodeUpdateTrustStatuses(
    releaseId: string,
): Promise<NodeUpdateTrustStatus[]> {
    const statuses: NodeUpdateTrustStatus[] = [];
    const encodedReleaseId = encodeURIComponent(releaseId);
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<NodeUpdateTrustStatus[]>(
            `/api/console/managed/updates/${encodedReleaseId}/node-trust/nodes`,
            { params: { after, limit: 100 } },
        );
        statuses.push(...response.data);
        if (response.data.length < 100) return statuses;
        const nextCursor = response.data.at(-1)?.node_id;
        if (!nextCursor || nextCursor === after) {
            throw new Error("A node update trust page did not advance its cursor identity");
        }
        after = nextCursor;
    }
}
