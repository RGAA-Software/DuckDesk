import { describe, expect, it, vi } from 'vitest'
import { exchangeRenewalTicket } from '../../../px_web_client/src/ticket_renewal'

describe('exchangeRenewalTicket', () => {
  it('sends the rotating capability and returns only the newly issued values', async () => {
    const fetchImpl = vi.fn(async () => new Response(JSON.stringify({
      code: 200,
      message: 'ok',
      data: {
        ticket: 'ticket-new',
        renewal_token: 'renew-new',
        permissions: ['view'],
      },
    }), { status: 200, headers: { 'Content-Type': 'application/json' } })) as unknown as typeof fetch

    const result = await exchangeRenewalTicket(
      fetchImpl,
      'https://console.local/api/v1/connection-tickets/renew',
      'renew-old',
      'browser-1',
    )

    expect(result).toEqual({
      ticket: 'ticket-new',
      renewalToken: 'renew-new',
      permissions: ['view'],
    })
    expect(fetchImpl).toHaveBeenCalledOnce()
    expect(JSON.parse(String(vi.mocked(fetchImpl).mock.calls[0]?.[1]?.body))).toEqual({
      renewal_token: 'renew-old',
      client_nonce: 'browser-1',
    })
  })

  it('refuses to reuse a consumed ticket when no renewal capability exists', async () => {
    await expect(exchangeRenewalTicket(fetch, '', '', 'browser-1')).rejects.toThrow(
      '请从 Console 重新进入',
    )
  })
})
