import type { AxiosResponse, InternalAxiosRequestConfig } from "axios";
import { afterEach, describe, expect, it, vi } from "vitest";
import {
    guestResourceHttp,
    publicHttp,
    setGuestToken,
    setUserToken,
    userHttp,
    userResourceHttp,
} from "./http";

function captureRequest(client: typeof userHttp) {
    let captured: InternalAxiosRequestConfig | undefined;
    client.defaults.adapter = async config => {
        captured = config;
        return {
            config,
            data: {},
            headers: {},
            status: 200,
            statusText: "OK",
        } satisfies AxiosResponse;
    };
    return () => captured;
}

afterEach(() => {
    sessionStorage.clear();
    vi.restoreAllMocks();
});

describe("new Console authorization headers", () => {
    it("binds user resources to user_web and the explicit user subject", async () => {
        setUserToken("1".repeat(64));
        const request = captureRequest(userResourceHttp);

        await userResourceHttp.get("/api/console/instances");

        expect(request()?.headers.get("Authorization")).toBe(`Bearer ${"1".repeat(64)}`);
        expect(request()?.headers.get("X-Pixels-Client-Type")).toBe("user_web");
        expect(request()?.headers.get("X-Pixels-Subject-Kind")).toBe("user");
    });

    it("never substitutes a user token or subject for a guest resource request", async () => {
        setUserToken("1".repeat(64));
        setGuestToken("2".repeat(64));
        const request = captureRequest(guestResourceHttp);

        await guestResourceHttp.get("/api/console/instances");

        expect(request()?.headers.get("Authorization")).toBe(`Bearer ${"2".repeat(64)}`);
        expect(request()?.headers.get("X-Pixels-Client-Type")).toBe("user_web");
        expect(request()?.headers.get("X-Pixels-Subject-Kind")).toBe("guest");
    });

    it("does not send a retired CSRF or subject header on identity requests", async () => {
        setUserToken("3".repeat(64));
        localStorage.setItem("px_user_csrf", "retired");
        const request = captureRequest(userHttp);

        await userHttp.get("/api/console/session");

        expect(request()?.headers.get("Authorization")).toBe(`Bearer ${"3".repeat(64)}`);
        expect(request()?.headers.has("X-CSRF-Token")).toBe(false);
        expect(request()?.headers.has("X-Pixels-Subject-Kind")).toBe(false);
    });

    it("never attaches an existing session to login or registration requests", async () => {
        setUserToken("4".repeat(64));
        setGuestToken("5".repeat(64));
        const request = captureRequest(publicHttp);

        await publicHttp.post("/api/console/sessions", { username: "user", password: "password" });

        expect(request()?.headers.has("Authorization")).toBe(false);
        expect(request()?.headers.get("X-Pixels-Client-Type")).toBe("user_web");
        expect(request()?.headers.has("X-Pixels-Subject-Kind")).toBe(false);
    });
});
