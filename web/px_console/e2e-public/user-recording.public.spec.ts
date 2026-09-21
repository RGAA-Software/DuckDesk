import { createHash } from "node:crypto";
import process from "node:process";
import { expect, test } from "@playwright/test";
import type { Download } from "@playwright/test";

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
    await page.getByPlaceholder(/文\s*件\s*名\s*或\s*会\s*话/).fill(expectedFileName);
    await page.getByPlaceholder(/文\s*件\s*名\s*或\s*会\s*话/).press("Enter");
    const recordingRow = page.getByRole("row").filter({ hasText: expectedFileName });
    await expect(recordingRow).toBeVisible();
    await expect(recordingRow).toContainText(/h264/i);

    let download: Download | null = null;
    for (let attempt = 0; attempt < 10 && !download; attempt += 1) {
        const downloadPromise = page.waitForEvent("download", { timeout: 5_000 }).catch(() => null);
        await recordingRow.getByRole("button", { name: /下\s*载/ }).click();
        download = await downloadPromise;
    }
    if (!download) throw new Error("The recording cache did not become downloadable.");
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
