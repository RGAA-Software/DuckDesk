import { describe, expect, it } from "vitest";
import en from "./en";
import zh from "./zh";

function catalogKeys(value: unknown, prefix = ""): string[] {
    if (typeof value !== "object" || value === null || Array.isArray(value)) return [prefix];
    return Object.entries(value).flatMap(([key, child]) =>
        catalogKeys(child, prefix ? `${prefix}.${key}` : key),
    );
}

describe("Console localization catalogs", () => {
    it("keeps English and Simplified Chinese keys identical", () => {
        expect(catalogKeys(en).sort()).toEqual(catalogKeys(zh).sort());
    });

    it("localizes every administrator navigation destination", () => {
        const navigationKeys = [
            "applications",
            "dashboard",
            "devices",
            "groups",
            "online",
            "profile",
            "security",
            "telemetryAlerts",
            "users",
        ];
        for (const key of navigationKeys) {
            expect(en.navigation[key as keyof typeof en.navigation]).toBeTruthy();
            expect(zh.navigation[key as keyof typeof zh.navigation]).toBeTruthy();
        }
    });
});
