import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    listManagedChannels,
    listManagedResourceSessions,
    listManagedVisits,
} from "./managed_activity_api";

vi.mock("@/http", () => ({ default: { get: vi.fn() } }));

describe("PostgreSQL managed activity API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("lists management sessions without requesting a descriptor or bearer ticket", async () => {
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: [] } as never);

        await listManagedResourceSessions();

        expect(axiosHttp.get).toHaveBeenCalledWith("/api/console/managed/resource-sessions", {
            params: { after: undefined, limit: 100 },
        });
    });

    it("uses the nested session identity as the visit cursor", async () => {
        const visits = Array.from({ length: 100 }, (_, index) => ({
            session: { id: `session-${index}` },
        }));
        vi.mocked(axiosHttp.get)
            .mockResolvedValueOnce({ data: visits } as never)
            .mockResolvedValueOnce({ data: [] } as never);

        await listManagedVisits();

        expect(axiosHttp.get).toHaveBeenNthCalledWith(2, "/api/console/managed/activity/visits", {
            params: { after: "session-99", limit: 100 },
        });
    });

    it("filters channels by the exact resource session", async () => {
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: [] } as never);

        await listManagedChannels("session-id");

        expect(axiosHttp.get).toHaveBeenCalledWith("/api/console/managed/activity/channels", {
            params: { session: "session-id", after: undefined, limit: 100 },
        });
    });
});
