import { describe, expect, it } from "vitest";
import { prepareDescriptorLaunchUrl } from "./api";

describe("prepareDescriptorLaunchUrl", () => {
    it("keeps the short-lived frontend token out of the initial HTTP request target", () => {
        const value = prepareDescriptorLaunchUrl({
            token: "a".repeat(64),
            descriptor: {
                session: {
                    id: "00000000-0000-4000-8000-000000000001",
                    target: {
                        kind: "cloud_application",
                        application_id: "00000000-0000-4000-8000-000000000002",
                        instance_id: "00000000-0000-4000-8000-000000000003",
                    },
                    client_type: "user_web",
                    access_role: "controller",
                    state: "pending",
                    revision: 2,
                    created_at: "2026-09-18T00:00:00Z",
                    closed_at: null,
                },
                host: "render.example.test",
                port: 4613,
                transport: "native",
                expires_at: "2026-09-18T00:00:30Z",
            },
        });
        const url = new URL(value);
        const fragment = new URLSearchParams(url.hash.slice(1));

        expect(url.origin).toBe("http://render.example.test:4613");
        expect(url.pathname).toBe("/web/");
        expect(url.searchParams.get("deviceId")).toBe("00000000-0000-4000-8000-000000000003");
        expect(url.searchParams.get("instanceId")).toBe("00000000-0000-4000-8000-000000000003");
        expect(url.searchParams.has("frontend_token")).toBe(false);
        expect(fragment.get("session_id")).toBe("00000000-0000-4000-8000-000000000001");
        expect(fragment.get("session_revision")).toBe("2");
        expect(fragment.get("frontend_token")).toBe("a".repeat(64));
        expect(fragment.get("perms")).toBe("view,input,clipboard,file,audio");
    });

    it("formats IPv6 endpoints and observer capabilities without a password fallback", () => {
        const value = prepareDescriptorLaunchUrl({
            token: "b".repeat(64),
            descriptor: {
                session: {
                    id: "00000000-0000-4000-8000-000000000004",
                    target: {
                        kind: "desktop",
                        device_id: "00000000-0000-4000-8000-000000000005",
                    },
                    client_type: "user_web",
                    access_role: "observer",
                    state: "pending",
                    revision: 7,
                    created_at: "2026-09-18T00:00:00Z",
                    closed_at: null,
                },
                host: "2001:db8::10",
                port: 4601,
                transport: "native",
                expires_at: "2026-09-18T00:00:30Z",
            },
        });
        const url = new URL(value);
        const fragment = new URLSearchParams(url.hash.slice(1));

        expect(url.host).toBe("[2001:db8::10]:4601");
        expect(url.searchParams.has("c")).toBe(false);
        expect(url.searchParams.has("password")).toBe(false);
        expect(fragment.get("perms")).toBe("view,audio");
    });
});
