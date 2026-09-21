import { readFileSync } from "node:fs";

export interface WebBuildBranding {
    applicationName: string;
    iconDataUrl: string;
    oemProfileSha256: string;
}

type BuildEnvironment = Record<string, string | undefined>;
type AssetReader = (path: string) => Buffer;

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const MAXIMUM_ICON_BYTES = 2 * 1024 * 1024;

export function resolveWebBuildBranding(
    distribution: string,
    environment: BuildEnvironment,
    defaultIconPath: string,
    assetReader: AssetReader = readFileSync,
): WebBuildBranding {
    const configuredApplicationName = environment.PIXELS_WEB_APPLICATION_NAME ?? "";
    const configuredIconPath = environment.PIXELS_WEB_ICON_FILE ?? "";
    const configuredOemProfileSha256 = environment.PIXELS_WEB_OEM_PROFILE_SHA256 ?? "";
    let applicationName = "Pixels";
    let iconPath = defaultIconPath;
    let oemProfileSha256 = "";

    if (distribution === "oem") {
        if (
            configuredApplicationName !== configuredApplicationName.trim() ||
            configuredApplicationName.length < 2 ||
            configuredApplicationName.length > 128 ||
            /[\u0000-\u001f]/.test(configuredApplicationName)
        ) {
            throw new Error("PIXELS_WEB_APPLICATION_NAME must contain the validated OEM application name");
        }
        if (!configuredIconPath) {
            throw new Error("PIXELS_WEB_ICON_FILE is required for an OEM Web Client build");
        }
        if (!/^[0-9a-f]{64}$/.test(configuredOemProfileSha256)) {
            throw new Error("PIXELS_WEB_OEM_PROFILE_SHA256 must contain the immutable OEM profile SHA-256");
        }
        applicationName = configuredApplicationName;
        iconPath = configuredIconPath;
        oemProfileSha256 = configuredOemProfileSha256;
    } else if (configuredApplicationName || configuredIconPath || configuredOemProfileSha256) {
        throw new Error("Development, Official, and Customer Web Client builds must not configure OEM branding");
    }

    const iconBytes = assetReader(iconPath);
    if (
        iconBytes.length < PNG_SIGNATURE.length ||
        iconBytes.length > MAXIMUM_ICON_BYTES ||
        !iconBytes.subarray(0, PNG_SIGNATURE.length).equals(PNG_SIGNATURE)
    ) {
        throw new Error("The Web Client brand icon must be a nonempty PNG no larger than 2 MiB");
    }
    return {
        applicationName,
        iconDataUrl: `data:image/png;base64,${iconBytes.toString("base64")}`,
        oemProfileSha256,
    };
}

export function escapeHtml(value: string): string {
    const replacements: Record<string, string> = {
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;",
    };
    return value.replace(/[&<>"']/g, (character) => replacements[character] ?? character);
}
