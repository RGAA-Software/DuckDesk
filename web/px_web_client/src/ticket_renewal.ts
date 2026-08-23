export interface RenewedTicket {
  ticket: string
  renewalToken: string
  permissions: string[]
}

interface RenewalEnvelope {
  code?: number
  message?: string
  data?: { ticket?: string; renewal_token?: string; permissions?: string[] }
}

export async function exchangeRenewalTicket(
  fetchImpl: typeof fetch,
  renewalUrl: string,
  renewalToken: string,
  clientNonce: string,
): Promise<RenewedTicket> {
  if (!renewalUrl || !renewalToken || !clientNonce) {
    throw new Error('一次性连接票据已使用，请从 Console 重新进入')
  }
  const response = await fetchImpl(renewalUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      renewal_token: renewalToken,
      client_nonce: clientNonce,
    }),
  })
  if (!response.ok) throw new Error(`重连票据申请失败: HTTP ${response.status}`)
  const payload = (await response.json()) as RenewalEnvelope
  if (payload.code !== 200 || !payload.data?.ticket || !payload.data.renewal_token) {
    throw new Error(payload.message || '重连票据申请失败')
  }
  return {
    ticket: payload.data.ticket,
    renewalToken: payload.data.renewal_token,
    permissions: payload.data.permissions ?? [],
  }
}
