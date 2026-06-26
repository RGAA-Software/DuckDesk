import { describe, it, expect, vi } from 'vitest'
import { webcrypto } from 'node:crypto'
import { calculateAppSecret, generateConnectionToken } from './auth_token'

vi.stubGlobal('crypto', webcrypto)

describe('auth_token', () => {
  it('calculates app_secret compatible with Rust/C++', () => {
    expect(calculateAppSecret('appkey-1')).toBe('ae1f850d1ab26da8d28f55d292397aac')
  })

  it('generates deterministic HMAC-SHA256 token', () => {
    const token = generateConnectionToken('appkey-1', 1234567890123, 'aabbccddeeff00112233445566778899')
    expect(token.ts).toBe(1234567890123)
    expect(token.nonce).toBe('aabbccddeeff00112233445566778899')
    expect(token.token).toBe('75498822dbf8e372461ce1a0843cb7b1ac0d81e537d18eef6fcbcb0ce3f66e2e')
  })

  it('generates random token with valid format', () => {
    const token = generateConnectionToken('appkey-1')
    expect(token.token).toMatch(/^[a-f0-9]{64}$/)
    expect(token.nonce).toMatch(/^[a-f0-9]{32}$/)
    expect(token.ts).toBeGreaterThan(0)
  })
})
