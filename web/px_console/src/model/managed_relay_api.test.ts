import { beforeEach, describe, expect, it, vi } from "vitest";
import axiosHttp from "@/http";
import {
    configureManagedRelay,
    createManagedRelay,
    listManagedRelays,
    type ManagedRelay,
} from "./managed_relay_api";

vi.mock("@/http", () => ({
    default: {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
    },
}));

function relayProfile(id: string, revision = 1): ManagedRelay {
    return {
        id,
        name: `relay-${id}`,
        public_host: "relay.example.test",
        public_port: 4605,
        revision,
        generation: 0,
        control_epoch: null,
        state: "offline",
        desired_draining: true,
        reported_draining: null,
        disabled: false,
        report_sequence: 0,
        last_seen: null,
        product_version_code: null,
        max_connections: null,
        current_connections: null,
        max_rooms: null,
        current_rooms: null,
        uploaded_bytes: null,
        forwarded_bytes: null,
        fresh: false,
    };
}

describe("managed Relay API", () => {
    beforeEach(() => vi.clearAllMocks());

    it("paginates the authoritative inventory", async () => {
        const firstPage = Array.from({ length: 100 }, (_, relayIndex) =>
            relayProfile(String(relayIndex).padStart(36, "0")),
        );
        const finalRelay = relayProfile("00000000-0000-0000-0000-000000000101");
        vi.mocked(axiosHttp.get)
            .mockResolvedValueOnce({ data: firstPage } as never)
            .mockResolvedValueOnce({ data: [finalRelay] } as never);

        await expect(listManagedRelays()).resolves.toHaveLength(101);
        expect(axiosHttp.get).toHaveBeenLastCalledWith("/api/console/managed/relays", {
            params: { after: firstPage.at(-1)?.id, limit: 100 },
        });
    });

    it("sends create and revision-protected drain changes", async () => {
        const relay = relayProfile("00000000-0000-0000-0000-000000000001", 7);
        vi.mocked(axiosHttp.post).mockResolvedValue({
            data: { relay, relay_token: "a".repeat(64) },
        } as never);
        vi.mocked(axiosHttp.patch).mockResolvedValue({
            data: { ...relay, revision: 8, desired_draining: false },
        } as never);

        await createManagedRelay("public-relay", "relay.example.test", 4605);
        await configureManagedRelay(relay, { draining: false, disabled: false });

        expect(axiosHttp.post).toHaveBeenCalledWith("/api/console/managed/relays", {
            name: "public-relay",
            public_host: "relay.example.test",
            public_port: 4605,
        });
        expect(axiosHttp.patch).toHaveBeenCalledWith(`/api/console/managed/relays/${relay.id}`, {
            revision: 7,
            configuration: { draining: false, disabled: false },
        });
    });
});
