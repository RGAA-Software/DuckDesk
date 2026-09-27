import process from "node:process";
import { expect, test } from "@playwright/test";

test.use({ ignoreHTTPSErrors: true });

test("administrator triggers and observes a new verified backup from the page", async ({ page }) => {
    test.setTimeout(120_000);
    const username = process.env.PIXELS_PUBLIC_CONSOLE_USERNAME;
    const password = process.env.PIXELS_PUBLIC_CONSOLE_PASSWORD;
    if (!username || !password) throw new Error("Public Console administrator credentials are required.");

    await page.goto("/");
    await page.locator('input[autocomplete="username"]').fill(username);
    await page.locator('input[autocomplete="current-password"]').fill(password);
    await page.getByRole("button", { name: /登\s*录/ }).click();
    await expect(page).toHaveURL(/\/resources$/);
    await page.goto("/apps");

    const backupCard = page.locator(".ant-card").filter({ hasText: "数据库备份" }).first();
    const triggerButton = backupCard.getByRole("button", { name: "立即备份" });
    await expect(triggerButton).toBeEnabled();
    const recoverySetCell = backupCard.getByRole("row", { name: /恢复集/ }).getByRole("cell").first();
    const previousRecoverySet = (await recoverySetCell.textContent())?.trim();
    if (!previousRecoverySet || !/^[0-9a-f-]{36}$/.test(previousRecoverySet)) {
        throw new Error("The previous recovery set is unavailable.");
    }

    await triggerButton.click();
    await expect(page.getByText("备份任务已接受")).toBeVisible();
    await expect(recoverySetCell).not.toHaveText(previousRecoverySet, { timeout: 90_000 });
    await expect(recoverySetCell).toHaveText(/^[0-9a-f-]{36}$/);
    await expect(triggerButton).toBeEnabled();
    const currentRecoverySet = (await recoverySetCell.textContent())?.trim();
    if (!currentRecoverySet) throw new Error("The new recovery set is unavailable.");
    const inventoryRow = backupCard.locator("tr.ant-table-row").filter({ hasText: currentRecoverySet });
    await expect(inventoryRow).toBeVisible();
    await inventoryRow.getByRole("button", { name: "只读预检" }).click();
    const preflight = page.getByRole("dialog", { name: "只读预检" });
    await expect(preflight).toContainText(currentRecoverySet);
    await expect(preflight).toContainText("恢复准入");
    await expect(preflight).toContainText("未评估");
});
