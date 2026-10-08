import { describe, expect, it, vi } from "vitest";
import { queryAdminSession } from "@/model/admin_session_api.ts";
import router from "./index";

vi.mock("@/model/admin_session_api.ts", () => ({ queryAdminSession: vi.fn() }));

describe("Console administrator entry", () => {
    it("opens the dashboard instead of the login form when a saved session is valid", async () => {
        vi.mocked(queryAdminSession).mockResolvedValue({
            id: "11111111-1111-1111-1111-111111111111",
            username: "Pixels",
            role: "admin",
            authorization_revision: 1,
            revision: 1,
            avatar_url: null,
            created_at: "2026-09-28T00:00:00Z",
        });

        await router.push("/");
        expect(router.currentRoute.value.path).toBe("/resources");
    });

    it("shows the login form when no valid session exists", async () => {
        vi.mocked(queryAdminSession).mockResolvedValue(null);

        await router.push("/");
        expect(router.currentRoute.value.path).toBe("/");
    });
});
