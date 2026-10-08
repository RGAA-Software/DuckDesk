import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    blockGuestSession,
    createAdminUser,
    listAllAdminUsers,
    replaceGroupIds,
    resetAdminUserPassword,
    type GroupView,
    type GuestSessionView,
} from "./identity_api";

vi.mock("@/http", () => ({
    default: {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        put: vi.fn(),
        delete: vi.fn(),
    },
}));

function managedUser(index: number) {
    return {
        id: `00000000-0000-0000-0000-${String(index).padStart(12, "0")}`,
        username: `user-${index}`,
        role: "user" as const,
        disabled: false,
        deleted_at: null,
        authorization_revision: 1,
        revision: 1,
        has_avatar: false,
        created_at: "2026-09-18T00:00:00Z",
    };
}

describe("PostgreSQL Console identity API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("walks UUID cursors until the server returns a short user page", async () => {
        const firstPage = Array.from({ length: 100 }, (_, index) => managedUser(index + 1));
        const secondPage = [managedUser(101)];
        vi.mocked(axiosHttp.get)
            .mockResolvedValueOnce({ data: firstPage } as never)
            .mockResolvedValueOnce({ data: secondPage } as never);

        const result = await listAllAdminUsers("USER-101");

        expect(result).toHaveLength(1);
        expect(result[0]?.uid).toBe(secondPage[0]?.id);
        expect(axiosHttp.get).toHaveBeenNthCalledWith(1, "/api/console/users", {
            params: { after: undefined, limit: 100 },
        });
        expect(axiosHttp.get).toHaveBeenNthCalledWith(2, "/api/console/users", {
            params: { after: firstPage.at(-1)?.id, limit: 100 },
        });
    });

    it("sends explicit passwords and never asks the server to generate or reveal one", async () => {
        const created = managedUser(1);
        vi.mocked(axiosHttp.post).mockResolvedValue({ data: created } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({
            data: { ...created, revision: 2 },
        } as never);

        const user = await createAdminUser({
            username: "operator",
            password: "valid-passphrase",
            role: "user",
        });
        await resetAdminUserPassword(user, "replacement-passphrase");

        expect(axiosHttp.post).toHaveBeenCalledWith("/api/console/users", {
            username: "operator",
            password: "valid-passphrase",
            role: "user",
        });
        expect(axiosHttp.patch).toHaveBeenCalledWith(`/api/console/users/${created.id}/password`, {
            revision: 1,
            password: "replacement-passphrase",
        });
    });

    it("replaces group members with optimistic concurrency", async () => {
        const group: GroupView = {
            gid: "10000000-0000-0000-0000-000000000001",
            name: "Operators",
            remark: "",
            member_count: 0,
            app_count: 0,
            version: 4,
        };
        vi.mocked(axiosHttp.put).mockResolvedValue({
            data: { id: group.gid, name: group.name, remark: "", revision: 5 },
        } as never);

        await replaceGroupIds("members", group, ["20000000-0000-0000-0000-000000000001"]);

        expect(axiosHttp.put).toHaveBeenCalledWith(`/api/console/groups/${group.gid}/members`, {
            revision: 4,
            members: ["20000000-0000-0000-0000-000000000001"],
        });
    });

    it("blocks the selected guest using its current revision", async () => {
        const guest: GuestSessionView = {
            id: "30000000-0000-0000-0000-000000000001",
            client_type: "android",
            created_at: "2026-09-18T00:00:00Z",
            expires_at: "2026-09-18T01:00:00Z",
            revoked_at: null,
            revision: 2,
            blocked: false,
        };
        vi.mocked(axiosHttp.post).mockResolvedValue({
            data: { ...guest, revision: 3, revoked_at: "2026-09-18T00:05:00Z" },
        } as never);

        await expect(blockGuestSession(guest, true)).resolves.toMatchObject({
            blocked: true,
            revision: 3,
        });
        expect(axiosHttp.post).toHaveBeenCalledWith(
            `/api/console/managed/guests/${guest.id}/block-source`,
            {
                revision: 2,
                lifetime_seconds: 86400,
            },
        );
    });
});
