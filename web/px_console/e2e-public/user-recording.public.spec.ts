import { createHash } from "node:crypto";
import process from "node:process";
import { expect, test } from "@playwright/test";

function requiredEnvironment(name: string): string {
    const value = process.env[name];
    if (!value) {
        throw new Error(`${name} is required for public Console acceptance.`);
    }
    return value;
}

test("authenticated user downloads the expected recording", async ({ page }) => {
    const username = requiredEnvironment("PIXELS_PUBLIC_CONSOLE_USERNAME");
    const password = requiredEnvironment("PIXELS_PUBLIC_CONSOLE_PASSWORD");
    const expectedFileName = requiredEnvironment("PIXELS_PUBLIC_RECORDING_FILE");
    const expectedHash = requiredEnvironment("PIXELS_PUBLIC_RECORDING_SHA256").toUpperCase();
    const expectedSize = Number.parseInt(requiredEnvironment("PIXELS_PUBLIC_RECORDING_SIZE"), 10);

    await page.goto("/user/login");
    await page.locator('input[autocomplete="username"]').fill(username);
    await page.locator('input[autocomplete="current-password"]').fill(password);
    await page.getByRole("button", { name: /登\s*录/ }).click();
    await expect(page).toHaveURL(/\/user\/home$/);

    await page.goto("/user/recordings");
    const recordingRow = page.getByRole("row").filter({ hasText: expectedFileName });
    await expect(recordingRow).toBeVisible();
    await expect(recordingRow).toContainText(/h264/i);

    const downloadPromise = page.waitForEvent("download");
    await recordingRow.getByRole("button", { name: /下\s*载/ }).click();
    const download = await downloadPromise;
    expect(download.suggestedFilename()).toBe(expectedFileName);

    const downloadPath = await download.path();
    expect(downloadPath).not.toBeNull();
    const bytes = await import("node:fs/promises").then(fileSystem =>
        fileSystem.readFile(downloadPath!),
    );
    expect(bytes.byteLength).toBe(expectedSize);
    expect(bytes.subarray(4, 8).toString("ascii")).toBe("ftyp");
    expect(createHash("sha256").update(bytes).digest("hex").toUpperCase()).toBe(expectedHash);
});
