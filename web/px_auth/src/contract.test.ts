import { beforeEach, expect, it, vi } from "vitest";
import { en } from "./locales/en";
import { zh } from "./locales/zh";
import { validTerms, requestIdentity, type Terms } from "./licenseModel";
import { ApiFailure, clearSession, request, signIn, signOut } from "./api";
import { language, theme, t } from "./uiSettings";
import { nextTick } from "vue";
beforeEach(() => {
    clearSession();
    vi.unstubAllGlobals();
});
it("requires catalog parity and reuses the same UI state for languages and themes", async () => {
    expect(Object.keys(en).sort()).toEqual(Object.keys(zh).sort());
    language.value = "en";
    theme.value = "light";
    await nextTick();
    expect(t("licenses")).toBe(en.licenses);
    expect(document.documentElement.dataset.theme).toBe("light");
    language.value = "zh";
    theme.value = "dark";
    await nextTick();
    expect(t("licenses")).toBe(zh.licenses);
    expect(localStorage.getItem("pixels_auth_theme")).toBe("dark");
});
it("validates UUID, signed integer boundaries, products, sorted capabilities and UTC expiration", () => {
    const terms: Terms = {
        customer_id: crypto.randomUUID(),
        deployment_id: crypto.randomUUID(),
        product: "pixels_console",
        distribution: "customer",
        machine_sha256: "a".repeat(64),
        mode: "trial",
        activation: { kind: "immediately" },
        expires_at: 2000000000,
        max_devices: 4294967295,
        max_sessions: 1,
        features: ["desktop", "rdp"],
    };
    expect(validTerms(terms, 1900000000)).toBe(true);
    for (const patch of [
        { max_devices: 4294967296 },
        { max_sessions: 0 },
        { expires_at: 1900000000 },
        { expires_at: Infinity },
        { machine_sha256: "A".repeat(64) },
        { deployment_id: "00000000-0000-0000-0000-000000000000" },
        { features: ["rdp", "desktop"] },
        { product: "console" },
    ]) {
        expect(validTerms({ ...terms, ...patch } as Terms, 1900000000)).toBe(false);
    }
});
it("retains request identity for unknown commits but resets after success or changed body", () => {
    const identity = requestIdentity(),
        first = identity.next({ max: 1 });
    expect(identity.next({ max: 1 })).toBe(first);
    expect(identity.next({ max: 2 })).not.toBe(first);
    identity.reset();
    expect(identity.next({ max: 1 })).not.toBe(first);
});
it("uses only the current bearer contract and revokes before forgetting the session", async () => {
    const token = "a".repeat(64);
    const fetch = vi
        .fn()
        .mockResolvedValueOnce(
            new Response(
                JSON.stringify({ token, profile: { id: "id", username: "user", role: "admin" } }),
                { status: 200 },
            ),
        )
        .mockResolvedValueOnce(new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", fetch);
    await signIn("user", "synthetic-password");
    await signOut();
    expect(fetch.mock.calls[0]![0]).toBe("/api/auth/sessions");
    expect(fetch.mock.calls[1]![1].headers.Authorization).toBe("Bearer " + token);
    expect(fetch.mock.calls[1]![1].credentials).toBe("omit");
    expect(sessionStorage.getItem("pixels_auth_session")).toBeNull();
});
it("failed logout retains identity for retry; expired authorization clears it", async () => {
    sessionStorage.setItem("pixels_auth_session", "a".repeat(64));
    vi.stubGlobal(
        "fetch",
        vi
            .fn()
            .mockResolvedValueOnce(new Response(null, { status: 503 }))
            .mockResolvedValueOnce(new Response(null, { status: 401 })),
    );
    await expect(signOut()).rejects.toBeInstanceOf(ApiFailure);
    expect(sessionStorage.getItem("pixels_auth_session")).not.toBeNull();
    await expect(request("/me")).rejects.toMatchObject({ key: "unauthorized" });
    expect(sessionStorage.getItem("pixels_auth_session")).toBeNull();
});
