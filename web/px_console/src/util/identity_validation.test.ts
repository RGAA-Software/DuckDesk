import { describe, expect, it } from 'vitest'
import { validateAdminUsername, validateInitialPassword } from './identity_validation'

describe('admin identity validation', () => {
  it('matches the server username policy', () => {
    expect(validateAdminUsername('d')).toBe('用户名需要 2–64 个字符')
    expect(validateAdminUsername('dd')).toBeUndefined()
    expect(validateAdminUsername(' user')).toBe('用户名首尾不能包含空格')
    expect(validateAdminUsername('user/name')).toBe('用户名不能包含 / 或 \\')
    expect(validateAdminUsername('valid-user')).toBeUndefined()
  })

  it('allows generated passwords and rejects short supplied passwords', () => {
    expect(validateInitialPassword('')).toBeUndefined()
    expect(validateInitialPassword('dd')).toContain('8–128')
    expect(validateInitialPassword('        ')).toBe('密码不能全部是空格')
    expect(validateInitialPassword('valid-passphrase')).toBeUndefined()
  })
})
