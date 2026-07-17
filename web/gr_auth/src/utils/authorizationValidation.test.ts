import { describe, expect, it } from 'vitest'

import {
  MAX_AUTH_DAYS,
  MAX_AUTH_NAME_LEN,
  MAX_AUTH_STREAMS,
  MAX_MACHINE_CODE_LEN,
  validateCreateAuthorization,
  validateUpdateAuthorization,
} from './authorizationValidation'

const validCreateForm = () => ({
  name: ' customer-a ',
  machine_code: ' machine-a ',
  days: '30',
  max_streams: '4',
  role: '1',
})

describe('authorization validation', () => {
  it('normalizes valid create input', () => {
    const result = validateCreateAuthorization(validCreateForm())

    expect(result).toEqual({
      ok: true,
      value: {
        name: 'customer-a',
        machine_code: 'machine-a',
        days: 30,
        max_streams: 4,
        role: 1,
        product: 'cms',
      },
    })
  })

  it('rejects missing create fields', () => {
    for (const field of ['name', 'machine_code', 'days', 'max_streams', 'role'] as const) {
      const form = validCreateForm()
      form[field] = ''

      expect(validateCreateAuthorization(form)).toMatchObject({
        ok: false,
        message: '请填写完整授权信息',
      })
    }
  })

  it('rejects empty and too-long create strings', () => {
    expect(validateCreateAuthorization({
      ...validCreateForm(),
      name: ' ',
    })).toMatchObject({ ok: false })

    expect(validateCreateAuthorization({
      ...validCreateForm(),
      name: 'x'.repeat(MAX_AUTH_NAME_LEN + 1),
    })).toMatchObject({ ok: false })

    expect(validateCreateAuthorization({
      ...validCreateForm(),
      machine_code: 'x'.repeat(MAX_MACHINE_CODE_LEN + 1),
    })).toMatchObject({ ok: false })
  })

  it('covers days lower, upper, and invalid boundaries', () => {
    for (const days of ['0', '-1', String(MAX_AUTH_DAYS + 1), '1.5', 'abc']) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        days,
      })).toMatchObject({ ok: false })
    }

    for (const days of ['1', String(MAX_AUTH_DAYS)]) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        days,
      })).toMatchObject({ ok: true })
    }
  })

  it('covers max stream lower, upper, and invalid boundaries', () => {
    for (const maxStreams of ['0', '-1', String(MAX_AUTH_STREAMS + 1), '1.5', 'abc']) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        max_streams: maxStreams,
      })).toMatchObject({ ok: false })
    }

    for (const maxStreams of ['1', String(MAX_AUTH_STREAMS)]) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        max_streams: maxStreams,
      })).toMatchObject({ ok: true })
    }
  })

  it('accepts only supported customer roles', () => {
    for (const role of ['1', '2', '3']) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        role,
      })).toMatchObject({ ok: true })
    }

    for (const role of ['0', '-1', '4', '1.5', 'admin']) {
      expect(validateCreateAuthorization({
        ...validCreateForm(),
        role,
      })).toMatchObject({ ok: false })
    }
  })

  it('normalizes valid update input', () => {
    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '4',
      role: '2',
    })).toEqual({
      ok: true,
      value: {
        days: 30,
        max_streams: 4,
        role: 2,
      },
    })
  })

  it('rejects invalid update input boundaries', () => {
    expect(validateUpdateAuthorization({
      days: '0',
      max_streams: '4',
      role: '1',
    })).toMatchObject({ ok: false })

    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '0',
      role: '1',
    })).toMatchObject({ ok: false })

    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '4',
      role: '4',
    })).toMatchObject({ ok: false })
  })
})
