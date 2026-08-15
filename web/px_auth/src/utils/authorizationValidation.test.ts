import { describe, expect, it } from 'vitest'

import {
  MAX_AUTH_DAYS,
  MAX_AUTH_NAME_LEN,
  MAX_AUTH_STREAMS,
  MAX_MACHINE_CODE_LEN,
  normalizeProduct,
  validateCreateAuthorization,
  validateUpdateAuthorization,
} from './authorizationValidation'

const validCreateForm = () => ({
  name: ' customer-a ',
  machine_code: ' machine-a ',
  days: '30',
  max_streams: '4',
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
        product: 'cms',
      },
    })
  })

  it('rejects missing create fields', () => {
    for (const field of ['name', 'machine_code', 'days', 'max_streams'] as const) {
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

  it('normalizes valid update input', () => {
    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '4',
    })).toEqual({
      ok: true,
      value: {
        days: 30,
        max_streams: 4,
      },
    })
  })

  it('rejects invalid update input boundaries', () => {
    expect(validateUpdateAuthorization({
      days: '0',
      max_streams: '4',
    })).toMatchObject({ ok: false })

    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '0',
    })).toMatchObject({ ok: false })
  })

  it('recognizes godesk_cms and labels its max_streams as Max Streams', () => {
    expect(normalizeProduct('godesk_cms')).toBe('godesk_cms')
    expect(normalizeProduct('gopico')).toBe('gopico')
    expect(normalizeProduct('unknown')).toBe('cms')

    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '0',
      product: 'godesk_cms',
    })).toMatchObject({
      ok: false,
      message: `Max Streams 必须在 1 到 ${MAX_AUTH_STREAMS} 之间`,
    })

    expect(validateUpdateAuthorization({
      days: '30',
      max_streams: '0',
      product: 'gopico',
    })).toMatchObject({
      ok: false,
      message: `Max Devices 必须在 1 到 ${MAX_AUTH_STREAMS} 之间`,
    })
  })
})
