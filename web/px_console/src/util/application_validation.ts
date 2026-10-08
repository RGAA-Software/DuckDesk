import axios from "axios";
import type { ApplicationSpec } from "@/model/managed_application_api";

export type ApplicationValidationKey =
    | "applications.validation.name"
    | "applications.validation.executable"
    | "applications.validation.arguments"
    | "applications.validation.bitrate"
    | "applications.validation.disconnectGrace";

function isControlCharacter(character: string): boolean {
    const codePoint = character.codePointAt(0) ?? 0;
    return codePoint <= 0x1f || (codePoint >= 0x7f && codePoint <= 0x9f);
}

function isAbsoluteExecutable(executable: string): boolean {
    if (
        !/^[a-z]:\\/i.test(executable) ||
        !executable.toLowerCase().endsWith(".exe") ||
        new TextEncoder().encode(executable).length > 2048
    )
        return false;
    if (
        Array.from(executable.slice(3)).some(
            character => isControlCharacter(character) || '/:"<>|?*'.includes(character),
        )
    )
        return false;
    return executable
        .slice(3)
        .split("\\")
        .every(component => {
            const stem = component.split(".")[0] ?? "";
            return (
                component.length > 0 &&
                component !== "." &&
                component !== ".." &&
                !/[ .]$/.test(component) &&
                !/^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])$/i.test(stem)
            );
        });
}

// Keep these checks aligned with storage/src/application_model.rs. Do not rewrite paths or arguments.
export function validateApplicationSpec(
    spec: ApplicationSpec,
): ApplicationValidationKey | undefined {
    const nameLength = Array.from(spec.name).length;
    if (
        nameLength < 1 ||
        nameLength > 128 ||
        spec.name.trim() !== spec.name ||
        Array.from(spec.name).some(isControlCharacter)
    ) {
        return "applications.validation.name";
    }
    if (
        !Number.isInteger(spec.disconnect_grace_seconds) ||
        spec.disconnect_grace_seconds < 1 ||
        spec.disconnect_grace_seconds > 3600
    )
        return "applications.validation.disconnectGrace";
    if (spec.launch.kind === "rdp") return undefined;
    const bitrate = spec.launch.video.bitrate_kbps;
    if (!Number.isInteger(bitrate) || bitrate < 128 || bitrate > 200_000)
        return "applications.validation.bitrate";
    if (spec.launch.kind === "game_hook") {
        if (!isAbsoluteExecutable(spec.launch.executable_path))
            return "applications.validation.executable";
        if (
            new TextEncoder().encode(spec.launch.arguments).length > 8192 ||
            Array.from(spec.launch.arguments).some(
                character => isControlCharacter(character) && character !== "\t",
            )
        ) {
            return "applications.validation.arguments";
        }
    }
    return undefined;
}

export function applicationSaveErrorKey(error: unknown) {
    if (axios.isAxiosError(error)) {
        switch (error.response?.status) {
            case 400:
                return "applications.messages.invalidInput";
            case 401:
                return "applications.messages.sessionExpired";
            case 403:
                return "applications.messages.rejected";
            case 409:
                return "applications.messages.conflict";
        }
    }
    return "applications.messages.saveFailed";
}
