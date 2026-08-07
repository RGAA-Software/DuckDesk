import { describe, it, expect } from 'vitest'
import { buildGameHookClientUrl, buildWebClientUrl } from './web_client_url'

describe('web_client_url', () => {
  it('builds desktop connect token URL', () => {
    const url = buildWebClientUrl('10.0.0.2', 20371, { deviceId: 'dev1', password: 'pw' })
    expect(url.startsWith('http://10.0.0.2:20371/web_client/?c=')).toBe(true)
    expect(url.includes('deviceId=')).toBe(false)
  })

  it('builds game-hook URL with deviceId and instanceId', () => {
    const url = buildGameHookClientUrl('127.0.0.1', 32000, {
      deviceId: 'machine-a',
      instanceId: 'inst-1',
    })
    expect(url).toBe(
      'http://127.0.0.1:32000/web_client/?deviceId=machine-a&instanceId=inst-1',
    )
  })
})
