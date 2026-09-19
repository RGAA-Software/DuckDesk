import process from "node:process";
import { defineConfig, devices } from "@playwright/test";

const publicConsoleUrl = process.env.PIXELS_PUBLIC_CONSOLE_URL;
if (!publicConsoleUrl) {
    throw new Error("PIXELS_PUBLIC_CONSOLE_URL is required for public Console acceptance.");
}

export default defineConfig({
    testDir: "./e2e-public",
    timeout: 60_000,
    expect: {
        timeout: 10_000,
    },
    workers: 1,
    reporter: "line",
    use: {
        baseURL: publicConsoleUrl,
        headless: true,
        trace: "off",
    },
    projects: [
        {
            name: "public-chromium",
            use: {
                ...devices["Desktop Chrome"],
                channel: "chrome",
            },
        },
    ],
});
