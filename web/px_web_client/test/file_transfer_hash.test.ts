import { describe, expect, it } from 'vitest'

import { sha256Hex, sha256HexSoftware } from '../src/rtc/file_transfer'

const encoder = new TextEncoder()

describe('file transfer SHA-256', () => {
  it('computes the empty input vector in software', () => {
    expect(sha256HexSoftware(new Uint8Array())).toBe(
      'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
    )
  })

  it('computes the abc vector in software', () => {
    expect(sha256HexSoftware(encoder.encode('abc'))).toBe(
      'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
    )
  })

  it('always returns a real digest through the public API', async () => {
    await expect(sha256Hex(encoder.encode('Pixels file transfer')))
      .resolves.toMatch(/^[a-f0-9]{64}$/)
  })
})
