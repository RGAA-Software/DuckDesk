import { describe, expect, it } from 'vitest'
import { prepareLaunchUrl } from './api'

describe('prepareLaunchUrl', () => {
  it('keeps secrets in the fragment and attaches the rotating renewal endpoint', () => {
    const value = prepareLaunchUrl({
      launch_url: 'http://device.local:32004/web_client/?deviceId=D-1#ticket=t-1&nonce=n-1',
      renewal_token: 'r-1',
      permissions: ['view'],
    })
    const url = new URL(value)
    const fragment = new URLSearchParams(url.hash.slice(1))

    expect(url.searchParams.get('ticket')).toBeNull()
    expect(fragment.get('ticket')).toBe('t-1')
    expect(fragment.get('renew')).toBe('r-1')
    expect(fragment.get('perms')).toBe('view')
    expect(fragment.get('renew_url')).toBe(
      `${window.location.origin}/api/v1/connection-tickets/renew`,
    )
  })
})
