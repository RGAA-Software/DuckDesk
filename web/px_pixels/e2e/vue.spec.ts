import { expect, test } from '@playwright/test';

test('renders the Pixels home page and primary actions', async ({ page }) => {
    await page.goto('/');
    await expect(page).toHaveURL(/\/main$/);
    await expect(page.getByRole('heading', { level: 1 })).toContainText('随时可达');
    await expect(
        page.getByRole('main').getByRole('button', { name: '咨询解决方案' }),
    ).toBeVisible();
    await expect(page.getByRole('button', { name: '探索产品能力' })).toBeVisible();
});
