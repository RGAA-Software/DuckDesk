import { beforeEach, describe, expect, it, vi } from "vitest";
import { getFileTransfersPage } from "./api";
import { userResourceHttp } from "./http";

vi.mock("./http", () => ({
    hasUserToken: vi.fn(),
    publicHttp: { post: vi.fn() },
    setUserToken: vi.fn(),
    userHttp: { get: vi.fn(), patch: vi.fn(), put: vi.fn(), delete: vi.fn() },
    userResourceHttp: { get: vi.fn(), post: vi.fn() },
}));

describe("user file-transfer history API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("lists only owner-scoped history without accepting node authority", async () => {
        vi.mocked(userResourceHttp.get).mockResolvedValue({ data: [] } as never);

        await getFileTransfersPage();

        expect(userResourceHttp.get).toHaveBeenCalledWith("/api/console/file-transfers", {
            params: { after: undefined, limit: 100 },
        });
    });

    it("filters the collected audit history by file, session, and state", async () => {
        vi.mocked(userResourceHttp.get).mockResolvedValue({
            data: [
                {
                    id: "transfer-1",
                    session_id: "session-alpha",
                    node_id: "node-1",
                    direction: "to_node",
                    file_name: "report.pdf",
                    total_bytes: 2048,
                    transferred_bytes: 2048,
                    state: "completed",
                    reason: null,
                    sequence: 2,
                    revision: 3,
                    created_at: "2026-09-20T00:00:00Z",
                    updated_at: "2026-09-20T00:00:01Z",
                    ended_at: "2026-09-20T00:00:01Z",
                },
                {
                    id: "transfer-2",
                    session_id: "session-beta",
                    node_id: "node-2",
                    direction: "from_node",
                    file_name: "archive.zip",
                    total_bytes: 4096,
                    transferred_bytes: 1024,
                    state: "failed",
                    reason: "transport_lost",
                    sequence: 1,
                    revision: 2,
                    created_at: "2026-09-20T00:01:00Z",
                    updated_at: "2026-09-20T00:01:01Z",
                    ended_at: "2026-09-20T00:01:01Z",
                },
            ],
        } as never);

        const result = await getFileTransfersPage(1, 10, "session-beta", "failed");

        expect(result.total).toBe(1);
        expect(result.items[0]?.file_name).toBe("archive.zip");
        expect(result.items[0]?.reason).toBe("transport_lost");
    });
});
