import type { AxiosResponse, InternalAxiosRequestConfig } from "axios";
import { afterEach, describe, expect, it } from "vitest";
import axiosHttp, { setAdminToken } from "@/http";
import {
    changeAdminPassword,
    loginAdmin,
    logoutAdmin,
    queryAdminSession,
} from "./admin_session_api";

function installAdapter(responseData: unknown) {
    let request: InternalAxiosRequestConfig | undefined;
    axiosHttp.defaults.adapter = async config => {
        request = config;
        return {
            config,
            data: responseData,
            headers: {},
            status: 200,
            statusText: "OK",
        } satisfies AxiosResponse;
    };
    return () => request;
}

afterEach(() => sessionStorage.clear());

describe("PostgreSQL Console administrator session", () => {
    it("logs in without a legacy cookie or CSRF header and stores the bearer in this tab", async () => {
        const profile = {
            id: "11111111-1111-1111-1111-111111111111",
            username: "administrator",
            role: "admin" as const,
            authorization_revision: 1,
            revision: 1,
            avatar_url: null,
            created_at: "2026-09-18T00:00:00Z",
        };
        const request = installAdapter({ token: "a".repeat(64), profile });

        await expect(loginAdmin("administrator", "password")).resolves.toEqual(profile);

        expect(request()?.url).toBe("/api/console/sessions");
        expect(request()?.headers.get("X-Pixels-Client-Type")).toBe("admin_web");
        expect(request()?.headers.has("Authorization")).toBe(false);
        expect(request()?.headers.has("X-CSRF-Token")).toBe(false);
        expect(sessionStorage.getItem("pixels.admin_web.token")).toBe("a".repeat(64));
    });

    it("uses the exact bearer for profile and logout then clears it", async () => {
        setAdminToken("b".repeat(64));
        const request = installAdapter({
            id: "11111111-1111-1111-1111-111111111111",
            username: "viewer",
            role: "viewer",
            authorization_revision: 1,
            revision: 1,
            avatar_url: null,
            created_at: "2026-09-18T00:00:00Z",
        });

        await expect(queryAdminSession()).resolves.toMatchObject({ username: "viewer" });
        expect(request()?.headers.get("Authorization")).toBe(`Bearer ${"b".repeat(64)}`);
        await logoutAdmin();
        expect(request()?.method).toBe("delete");
        expect(request()?.url).toBe("/api/console/session");
        expect(sessionStorage.getItem("pixels.admin_web.token")).toBeNull();
    });

    it("changes the password through the PostgreSQL identity endpoint", async () => {
        setAdminToken("c".repeat(64));
        const request = installAdapter(null);

        await changeAdminPassword("current-passphrase", "replacement-passphrase");

        expect(request()?.method).toBe("patch");
        expect(request()?.url).toBe("/api/console/password");
        expect(JSON.parse(String(request()?.data))).toEqual({
            current_password: "current-passphrase",
            new_password: "replacement-passphrase",
        });
    });
});
