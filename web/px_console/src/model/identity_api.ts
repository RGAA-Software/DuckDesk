import axiosHttp from "@/http";

export type ManagedRole = "user" | "admin" | "viewer";

interface ManagedUserWire {
    id: string;
    username: string;
    role: ManagedRole;
    disabled: boolean;
    deleted_at: string | null;
    authorization_revision: number;
    revision: number;
    has_avatar: boolean;
    created_at: string;
}

export interface UserAdminView {
    uid: string;
    username: string;
    role: ManagedRole;
    disabled: boolean;
    deleted_at: string | null;
    auth_version: number;
    version: number;
    has_avatar: boolean;
    created_at: string;
}

interface GroupWire {
    id: string;
    name: string;
    remark: string;
    revision: number;
}

export interface GroupView {
    gid: string;
    name: string;
    remark: string;
    member_count: number;
    app_count: number;
    version: number;
}

export interface GuestSessionView {
    id: string;
    client_type: string;
    created_at: string;
    expires_at: string;
    revoked_at: string | null;
    revision: number;
    blocked: boolean;
}

export interface AppOption {
    app_id: string;
    name: string;
    access_mode: "public" | "acl";
    group_ids: string[];
    version: number;
}

async function collectPages<T extends { id: string }>(path: string): Promise<T[]> {
    const result: T[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<T[]>(path, { params: { after, limit: 100 } });
        result.push(...response.data);
        if (response.data.length < 100) return result;
        after = response.data.at(-1)?.id;
        if (!after)
            throw new Error("A paginated Console response did not include its cursor identity");
    }
}

function userView(user: ManagedUserWire): UserAdminView {
    return {
        uid: user.id,
        username: user.username,
        role: user.role,
        disabled: user.disabled,
        deleted_at: user.deleted_at,
        auth_version: user.authorization_revision,
        version: user.revision,
        has_avatar: user.has_avatar,
        created_at: user.created_at,
    };
}

function groupView(group: GroupWire, memberCount = 0): GroupView {
    return {
        gid: group.id,
        name: group.name,
        remark: group.remark,
        member_count: memberCount,
        app_count: 0,
        version: group.revision,
    };
}

export async function listAllAdminUsers(keyword = ""): Promise<UserAdminView[]> {
    const normalizedKeyword = keyword.trim().toLocaleLowerCase();
    const users = (await collectPages<ManagedUserWire>("/api/console/users")).map(userView);
    if (!normalizedKeyword) return users;
    return users.filter(user => user.username.toLocaleLowerCase().includes(normalizedKeyword));
}

export async function listAdminUsers(page = 1, keyword = "", pageSize = 20) {
    const users = await listAllAdminUsers(keyword);
    const start = Math.max(0, page - 1) * pageSize;
    return { items: users.slice(start, start + pageSize), total: users.length };
}

export async function createAdminUser(request: {
    username: string;
    password: string;
    role: ManagedRole;
}) {
    const response = await axiosHttp.post<ManagedUserWire>("/api/console/users", request);
    return userView(response.data);
}

export async function patchAdminUser(
    user: UserAdminView,
    request: { role: ManagedRole; disabled: boolean },
) {
    const response = await axiosHttp.patch<ManagedUserWire>(
        `/api/console/users/${encodeURIComponent(user.uid)}`,
        {
            revision: user.version,
            ...request,
        },
    );
    return userView(response.data);
}

export async function deleteAdminUser(user: UserAdminView): Promise<void> {
    await axiosHttp.delete(`/api/console/users/${encodeURIComponent(user.uid)}`, {
        params: { revision: user.version },
    });
}

export async function resetAdminUserPassword(user: UserAdminView, password: string) {
    const response = await axiosHttp.patch<ManagedUserWire>(
        `/api/console/users/${encodeURIComponent(user.uid)}/password`,
        {
            revision: user.version,
            password,
        },
    );
    return userView(response.data);
}

export async function listGuestSessions(): Promise<GuestSessionView[]> {
    return collectPages<GuestSessionView>("/api/console/managed/guests");
}

export async function blockGuestSession(
    guest: GuestSessionView,
    includeSource: boolean,
): Promise<GuestSessionView> {
    const suffix = includeSource ? "block-source" : "block";
    const body = includeSource
        ? { revision: guest.revision, lifetime_seconds: 86400 }
        : { revision: guest.revision };
    const response = await axiosHttp.post<GuestSessionView>(
        `/api/console/managed/guests/${encodeURIComponent(guest.id)}/${suffix}`,
        body,
    );
    return { ...guest, ...response.data, blocked: true };
}

export async function listGroups(): Promise<GroupView[]> {
    const groups = await collectPages<GroupWire>("/api/console/groups");
    return Promise.all(
        groups.map(async group => groupView(group, (await groupIds("members", group.id)).length)),
    );
}

export async function createGroup(name: string, remark: string): Promise<GroupView> {
    const response = await axiosHttp.post<GroupWire>("/api/console/groups", { name, remark });
    return groupView(response.data);
}

export async function deleteGroup(group: GroupView): Promise<void> {
    await axiosHttp.delete(`/api/console/groups/${encodeURIComponent(group.gid)}`, {
        params: { revision: group.version },
    });
}

export async function groupIds(kind: "members" | "apps", groupId: string): Promise<string[]> {
    if (kind !== "members")
        throw new Error("Application group membership uses the managed application API");
    const response = await axiosHttp.get<string[]>(
        `/api/console/groups/${encodeURIComponent(groupId)}/members`,
    );
    return response.data;
}

export async function replaceGroupIds(
    kind: "members" | "apps",
    group: GroupView,
    ids: string[],
): Promise<GroupView> {
    if (kind !== "members")
        throw new Error("Application group membership uses the managed application API");
    const response = await axiosHttp.put<GroupWire>(
        `/api/console/groups/${encodeURIComponent(group.gid)}/members`,
        {
            revision: group.version,
            members: ids,
        },
    );
    return groupView(response.data, ids.length);
}

const unwrap = <T>(response: { data: { data: T } }) => response.data.data;
export async function listAppOptions() {
    return unwrap<AppOption[]>(await axiosHttp.get("/api/v1/admin/catalog/apps"));
}
export async function updateAppAccess(
    app: AppOption,
    accessMode: "public" | "acl",
    groupIds: string[],
) {
    return unwrap<AppOption>(
        await axiosHttp.patch(`/api/v1/admin/apps/${app.app_id}/access`, {
            version: app.version,
            access_mode: accessMode,
            group_ids: groupIds,
        }),
    );
}
