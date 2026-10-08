import { beforeEach, describe, expect, it, vi } from "vitest";

const guestState = vi.hoisted(() => ({ token: "" }));
const guestHttp = vi.hoisted(() => ({ get: vi.fn(), post: vi.fn() }));
vi.mock("./http", () => ({
    guestHttp,
    guestResourceHttp: guestHttp,
    publicHttp: { post: vi.fn() },
    hasGuestToken: () => Boolean(guestState.token),
    setGuestToken: (token: string) => {
        guestState.token = token;
    },
}));

import { ensureGuestSession, getPublicApps } from "./public_api";

describe("persistent public guest identity", () => {
    beforeEach(() => {
        vi.clearAllMocks();
        vi.useRealTimers();
        guestState.token = "";
    });

    it("keeps the same guest after thirty days without a lifetime field", async () => {
        vi.useFakeTimers();
        guestHttp.post.mockResolvedValue({
            data: { token: "guest-token", session: { id: "guest-1" } },
        });
        await ensureGuestSession();
        vi.advanceTimersByTime(30 * 24 * 60 * 60 * 1000);
        await ensureGuestSession();
        expect(guestHttp.post).toHaveBeenCalledTimes(1);
        expect(guestState.token).toBe("guest-token");
        vi.useRealTimers();
    });

    it("does not replace an explicitly rejected guest with a new owner", async () => {
        guestState.token = "blocked-guest-token";
        const rejection = { response: { status: 403, data: { code: "rejected" } } };
        guestHttp.get.mockRejectedValue(rejection);
        await expect(getPublicApps()).rejects.toBe(rejection);
        expect(guestHttp.get).toHaveBeenCalledTimes(1);
        expect(guestHttp.post).not.toHaveBeenCalled();
        expect(guestState.token).toBe("blocked-guest-token");
    });
});
