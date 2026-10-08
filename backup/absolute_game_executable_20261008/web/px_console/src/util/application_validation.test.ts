import { describe, expect, it } from "vitest";
import type { ApplicationSpec } from "@/model/managed_application_api";
import { applicationSaveErrorKey, validateApplicationSpec } from "./application_validation";
import english from "@/locales/en";
import chinese from "@/locales/zh";

function gameSpec(executable = "2dAdventure.exe", argumentsText = ""): ApplicationSpec {
    return {
        name: "2D Adventure",
        access: "public",
        launch: {
            kind: "game_hook",
            executable_relative: executable,
            arguments: argumentsText,
            video: { codec: "h264", bitrate_kbps: 20000 },
        },
        allow_observer: false,
        allow_takeover: false,
        disabled: false,
    };
}

describe("application editor validation", () => {
    it.each([
        "D:\\software\\2dadventure\\2dAdventure.exe",
        "\\\\server\\share\\game.exe",
        "games/game.exe",
        '"game.exe"',
        "..\\game.exe",
        "games\\..\\game.exe",
        "NUL.exe",
        "COM¹.exe",
        "dir.\\game.exe",
        "",
    ])("rejects invalid Windows relative executable %s before submission", executable => {
        expect(validateApplicationSpec(gameSpec(executable))).toBe(
            "applications.validation.executable",
        );
    });

    it.each(["2dAdventure.exe", "游戏\\版本 1\\启动 器.EXE", " leading space.exe"])(
        "preserves valid executable %s",
        executable => {
            const spec = gameSpec(executable, '--title "参数 保留"\t-windowed');
            expect(validateApplicationSpec(spec)).toBeUndefined();
            expect(spec.launch).toMatchObject({ executable_relative: executable });
        },
    );

    it("rejects multiline arguments and uses UTF-8 byte limits", () => {
        expect(validateApplicationSpec(gameSpec("game.exe", "-windowed\n-title test"))).toBe(
            "applications.validation.arguments",
        );
        expect(validateApplicationSpec(gameSpec("game.exe", "中".repeat(2731)))).toBe(
            "applications.validation.arguments",
        );
        expect(validateApplicationSpec(gameSpec("中".repeat(682) + ".exe"))).toBe(
            "applications.validation.executable",
        );
    });

    it("checks integer bitrate and name controls without requiring video fields for RDP", () => {
        const spec = gameSpec();
        if (spec.launch.kind !== "game_hook")
            throw new Error("Game fixture must have game launch settings");
        spec.launch.video.bitrate_kbps = 128.5;
        expect(validateApplicationSpec(spec)).toBe("applications.validation.bitrate");
        spec.launch = { kind: "rdp" };
        expect(validateApplicationSpec(spec)).toBeUndefined();
        spec.name = "Game\n";
        expect(validateApplicationSpec(spec)).toBe("applications.validation.name");
    });

    it("maps API failures to localized messages instead of displaying raw Axios errors", () => {
        expect(applicationSaveErrorKey({ isAxiosError: true, response: { status: 400 } })).toBe(
            "applications.messages.invalidInput",
        );
        expect(applicationSaveErrorKey({ isAxiosError: true, response: { status: 409 } })).toBe(
            "applications.messages.conflict",
        );
        expect(applicationSaveErrorKey(new Error("connection refused"))).toBe(
            "applications.messages.saveFailed",
        );
        expect(Object.keys(english.applications.validation).sort()).toEqual(
            Object.keys(chinese.applications.validation).sort(),
        );
        expect(Object.keys(english.applications.messages).sort()).toEqual(
            Object.keys(chinese.applications.messages).sort(),
        );
    });
});
