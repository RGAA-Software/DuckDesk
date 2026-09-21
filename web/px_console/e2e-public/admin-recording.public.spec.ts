import process from "node:process";
import { expect, test } from "@playwright/test";

function requiredEnvironment(name: string): string {
    const value = process.env[name];
    if (!value) {
        throw new Error(`${name} is required for public Console acceptance.`);
    }
    return value;
}

test("administrator retains, releases, and evicts the expected recording cache", async ({
    page,
}) => {
    const username = requiredEnvironment("PIXELS_PUBLIC_CONSOLE_USERNAME");
    const password = requiredEnvironment("PIXELS_PUBLIC_CONSOLE_PASSWORD");
    const expectedFileName = requiredEnvironment("PIXELS_PUBLIC_RECORDING_FILE");

    await page.goto("/");
    await page.locator('input[autocomplete="username"]').fill(username);
    await page.locator('input[autocomplete="current-password"]').fill(password);
    await page.getByRole("button", { name: /登\s*录/ }).click();
    await expect(page).toHaveURL(/\/resources$/);

    await page.goto("/security-internal");
    await page.getByRole("tab", { name: /录\s*像\s*元\s*数\s*据/ }).click();
    const recordingRow = page.getByRole("row").filter({ hasText: expectedFileName });
    for (let pageIndex = 0; pageIndex < 5 && (await recordingRow.count()) === 0; pageIndex += 1) {
        const nextPage = page.locator(".ant-pagination-next:not(.ant-pagination-disabled) button");
        if ((await nextPage.count()) === 0) break;
        await nextPage.click();
    }
    await expect(recordingRow).toBeVisible();
    await expect(recordingRow).toContainText("ready");

    await recordingRow.getByRole("button", { name: /保\s*留\s*副\s*本/ }).click();
    await expect(recordingRow.getByRole("button", { name: /取\s*消\s*保\s*留/ })).toBeVisible();

    await recordingRow.getByRole("button", { name: /取\s*消\s*保\s*留/ }).click();
    await expect(recordingRow.getByRole("button", { name: /保\s*留\s*副\s*本/ })).toBeVisible();

    await recordingRow.getByRole("button", { name: /驱\s*逐\s*副\s*本/ }).click();
    await page.getByRole("button", { name: "OK", exact: true }).click();
    await expect(recordingRow).toContainText(/未\s*缓\s*存/);
});
