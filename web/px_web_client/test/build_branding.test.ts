import { describe, expect, it, vi } from "vitest";
import { escapeHtml, resolveWebBuildBranding } from "../build_branding";

const PNG_BYTES = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    Buffer.from("fixture"),
]);

describe("Web release branding", () => {
    it("uses the Pixels identity when no OEM profile is active", () => {
        const assetReader = vi.fn(() => PNG_BYTES);
        const branding = resolveWebBuildBranding("official", {}, "pixels.png", assetReader);

        expect(branding.applicationName).toBe("Pixels");
        expect(branding.oemProfileSha256).toBe("");
        expect(branding.iconDataUrl).toMatch(/^data:image\/png;base64,/);
        expect(assetReader).toHaveBeenCalledWith("pixels.png");
    });

    it("binds all OEM Web branding inputs", () => {
        const branding = resolveWebBuildBranding(
            "oem",
            {
                PIXELS_WEB_APPLICATION_NAME: "Acme Cloud",
                PIXELS_WEB_ICON_FILE: "acme.png",
                PIXELS_WEB_OEM_PROFILE_SHA256: "a".repeat(64),
            },
            "pixels.png",
            () => PNG_BYTES,
        );

        expect(branding.applicationName).toBe("Acme Cloud");
        expect(branding.oemProfileSha256).toBe("a".repeat(64));
    });

    it("rejects incomplete OEM branding and cross-distribution pollution", () => {
        expect(() => resolveWebBuildBranding("oem", {}, "pixels.png", () => PNG_BYTES)).toThrow(/APPLICATION_NAME/);
        expect(() => resolveWebBuildBranding(
            "customer",
            { PIXELS_WEB_APPLICATION_NAME: "Acme Cloud" },
            "pixels.png",
            () => PNG_BYTES,
        )).toThrow(/must not configure OEM branding/);
    });

    it("rejects a non-PNG brand asset", () => {
        expect(() => resolveWebBuildBranding(
            "oem",
            {
                PIXELS_WEB_APPLICATION_NAME: "Acme Cloud",
                PIXELS_WEB_ICON_FILE: "acme.png",
                PIXELS_WEB_OEM_PROFILE_SHA256: "a".repeat(64),
            },
            "pixels.png",
            () => Buffer.from("not-png"),
        )).toThrow(/must be a nonempty PNG/);
    });

    it("escapes the application name before inserting it into index HTML", () => {
        expect(escapeHtml("A&B <Cloud>")).toBe("A&amp;B &lt;Cloud&gt;");
    });
});
