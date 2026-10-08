import process from "node:process";
import { expect, test } from "@playwright/test";

test.use({ ignoreHTTPSErrors: true });

test("administrator stays signed in when Console opens in a new tab", async ({ context, page }) => {
    const username = process.env.PIXELS_PUBLIC_CONSOLE_USERNAME;
    const password = process.env.PIXELS_PUBLIC_CONSOLE_PASSWORD;
    if (!username || !password) throw new Error("Console administrator credentials are required.");

    await page.goto("/");
    await page.locator('input[autocomplete="username"]').fill(username);
    await page.locator('input[autocomplete="current-password"]').fill(password);
    await page.getByRole("button", { name: /登\s*录/ }).click();
    await expect(page).toHaveURL(/\/resources$/);

    const newTab = await context.newPage();
    await newTab.goto("/");
    await expect(newTab).toHaveURL(/\/resources$/);
    await expect(newTab.locator('input[autocomplete="current-password"]')).toHaveCount(0);
});
