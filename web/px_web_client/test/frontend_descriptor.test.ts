import { describe, expect, it } from "vitest";
import {
    appendFrontendAuthorization,
    takeFrontendDescriptor,
} from "../src/rtc/frontend_descriptor";

describe("Console frontend descriptor", () => {
    it("extracts a complete grant and removes its secret from the visible fragment", () => {
        const fragment = new URLSearchParams({
            session_id: "00000000-0000-4000-8000-000000000001",
            session_revision: "2",
            frontend_token: "a".repeat(64),
            perms: "view,input",
        });

        const parsed = takeFrontendDescriptor(fragment);

        expect(parsed.incomplete).toBe(false);
        expect(parsed.removedToken).toBe(true);
        expect(parsed.descriptor).toEqual({
            sessionId: "00000000-0000-4000-8000-000000000001",
            sessionRevision: "2",
            token: "a".repeat(64),
        });
        expect(fragment.has("frontend_token")).toBe(false);
        expect(fragment.get("perms")).toBe("view,input");
    });

    it("does not forward a partial descriptor or invent a password fallback", () => {
        const fragment = new URLSearchParams({
            session_id: "00000000-0000-4000-8000-000000000001",
            frontend_token: "b".repeat(64),
        });
        const parsed = takeFrontendDescriptor(fragment);
        const query = new URLSearchParams({ device_id: "instance-id" });

        expect(() =>
            appendFrontendAuthorization(
                query,
                parsed.descriptor,
                "must-not-be-forwarded",
                parsed.incomplete,
            ),
        ).toThrow("Console frontend descriptor is incomplete");

        expect(parsed.incomplete).toBe(true);
        expect(parsed.descriptor).toBeNull();
        expect(query.has("session_id")).toBe(false);
        expect(query.has("frontend_token")).toBe(false);
        expect(query.has("safety_pwd_md5")).toBe(false);
    });

    it("copies a complete descriptor into the Render admission request", () => {
        const query = new URLSearchParams();
        appendFrontendAuthorization(
            query,
            {
                sessionId: "00000000-0000-4000-8000-000000000001",
                sessionRevision: "9",
                token: "c".repeat(64),
            },
            "must-not-be-forwarded",
        );

        expect(query.get("session_id")).toBe("00000000-0000-4000-8000-000000000001");
        expect(query.get("session_revision")).toBe("9");
        expect(query.get("frontend_token")).toBe("c".repeat(64));
        expect(query.has("safety_pwd_md5")).toBe(false);
    });
});
