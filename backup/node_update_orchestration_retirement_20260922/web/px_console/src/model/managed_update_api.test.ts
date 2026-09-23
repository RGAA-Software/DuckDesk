import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    getNodeUpdateTrustSummary,
    listNodeUpdateTrustStatuses,
    type NodeUpdateTrustStatus,
} from "./managed_update_api";

vi.mock("@/http", () => ({ default: { get: vi.fn() } }));

function status(index: number): NodeUpdateTrustStatus {
    return {
        node_id: `node-${String(index).padStart(3, "0")}`,
        device_id: `device-${index}`,
        node_state: "connected",
        disabled: false,
        last_seen: null,
        observed_release_id: null,
        repository_publication_sha256: null,
        trusted_root_version: null,
        observed_at: null,
        confirmed: false,
    };
}

describe("managed update trust API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("loads every stable node cursor page", async () => {
        const firstPage = Array.from({ length: 100 }, (_, index) => status(index));
        vi.mocked(axiosHttp.get)
            .mockResolvedValueOnce({ data: firstPage } as never)
            .mockResolvedValueOnce({ data: [status(100)] } as never);

        await expect(listNodeUpdateTrustStatuses("release/id")).resolves.toHaveLength(101);
        expect(axiosHttp.get).toHaveBeenNthCalledWith(
            2,
            "/api/console/managed/updates/release%2Fid/node-trust/nodes",
            { params: { after: "node-099", limit: 100 } },
        );
    });

    it("loads the release-scoped summary", async () => {
        vi.mocked(axiosHttp.get).mockResolvedValue({ data: { required_root_version: 7 } } as never);
        await expect(getNodeUpdateTrustSummary("release/id")).resolves.toEqual({
            required_root_version: 7,
        });
        expect(axiosHttp.get).toHaveBeenCalledWith(
            "/api/console/managed/updates/release%2Fid/node-trust",
        );
    });
});
