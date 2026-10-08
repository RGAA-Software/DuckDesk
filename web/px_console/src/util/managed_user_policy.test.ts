import { describe, expect, it } from "vitest";
import { isProtectedUser } from "./managed_user_policy";

describe("protected Pixels account", () => {
    it("matches the case-insensitive immutable account name", () => {
        for (const username of ["Pixels", "pixels", "PIXELS", "pIxElS"]) {
            expect(isProtectedUser({ username })).toBe(true);
        }
        for (const username of ["test", "Pixels-admin", "another-admin"]) {
            expect(isProtectedUser({ username })).toBe(false);
        }
    });
});
