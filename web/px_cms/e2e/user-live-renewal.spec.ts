import { expect, test } from '@playwright/test'

const LIVE = process.env.PX_LIVE_USER_RENEWAL === '1'
const CMS = process.env.PX_LIVE_CMS_URL || 'https://127.0.0.1:30500'

test('live Web Client rotates its ticket and reconnects after peer failure', async ({ browser }) => {
  test.skip(!LIVE, 'Set PX_LIVE_USER_RENEWAL=1 to run against the local CMS/Service/Render stack')
  test.setTimeout(120_000)

  const context = await browser.newContext({ ignoreHTTPSErrors: true })
  const page = await context.newPage()
  let instanceId = ''
  let csrf = ''
  try {
    await page.goto(`${CMS}/user/public-apps`)
    const viewOnly = page.getByRole('button', { name: '仅观看' }).first()
    await expect(viewOnly).toBeVisible({ timeout: 15_000 })
    await expect.poll(() => page.evaluate(() => sessionStorage.getItem('px_guest_csrf') || '')).not.toBe('')
    csrf = await page.evaluate(() => sessionStorage.getItem('px_guest_csrf') || '')

    const ticketRequest = page.waitForRequest(
      (request) => /\/api\/v1\/public\/instances\/[^/]+\/ticket$/.test(new URL(request.url()).pathname),
      { timeout: 45_000 },
    )
    await viewOnly.click()
    const ticketPath = new URL((await ticketRequest).url()).pathname
    instanceId = decodeURIComponent(ticketPath.split('/').at(-2) || '')
    expect(instanceId).not.toBe('')

    await page.waitForURL(/\/web_client\//, { timeout: 45_000 })
    await expect.poll(
      () => page.evaluate(() => (window as any).__conn?.status?.() || ''),
      { timeout: 45_000 },
    ).toBe('connected')
    await expect.poll(
      () => page.locator('video').evaluate((video: HTMLVideoElement) => video.currentTime),
      { timeout: 30_000 },
    ).toBeGreaterThan(0.5)

    const before = await page.locator('video').evaluate((video: HTMLVideoElement) => video.currentTime)
    const renewalResponse = page.waitForResponse(
      (response) => new URL(response.url()).pathname === '/api/v1/connection-tickets/renew',
      { timeout: 30_000 },
    )
    await page.evaluate(() => (window as any).__pc?.close())
    const renewed = await renewalResponse
    expect(renewed.status()).toBe(200)
    expect((await renewed.json()).code).toBe(200)
    await expect.poll(
      () => page.evaluate(() => (window as any).__conn?.status?.() || ''),
      { timeout: 45_000 },
    ).toBe('connected')
    await expect.poll(
      () => page.locator('video').evaluate((video: HTMLVideoElement) => video.currentTime),
      { timeout: 30_000 },
    ).toBeGreaterThan(before + 0.5)
  } finally {
    if (instanceId && csrf) {
      await context.request.post(`${CMS}/api/v1/public/instances/${encodeURIComponent(instanceId)}/stop`, {
        headers: { Origin: CMS, 'X-CSRF-Token': csrf },
        data: { reason: 'live_renewal_e2e_cleanup' },
      }).catch(() => undefined)
    }
    await context.close()
  }
})
