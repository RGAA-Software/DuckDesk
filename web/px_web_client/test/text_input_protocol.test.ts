import { describe, expect, it } from 'vitest'
import { PxMessage, MSG_TYPE_APPLICATION_TEXT_SUBMIT, MSG_TYPE_TEXT_INPUT } from '../src/rtc/proto'

describe('application text protocol', () => {
  it('keeps legacy semantics separate and preserves generations beyond JS safe integers', () => {
    const fields = {
      type: MSG_TYPE_APPLICATION_TEXT_SUBMIT,
      applicationTextSubmit: {
        requestId: 'request-1',
        target: { instanceId: 'app-a', leaseGeneration: '9007199254740993', targetGeneration: '2' },
        text: '中😀\n',
        inputGeneration: '7',
      },
    }
    expect(MSG_TYPE_TEXT_INPUT).toBe(580)
    expect(MSG_TYPE_APPLICATION_TEXT_SUBMIT).toBe(612)
    expect(PxMessage.verify(fields)).toBeNull()
    const bytes = PxMessage.encode(PxMessage.create(fields)).finish()
    // Shared golden bytes also asserted by the native protobuf consumer.
    const golden = '50e404a226360a09726571756573742d31121c0a056170702d611210393030373139393235343734303939331a01321a08e4b8adf09f98800a220137'
    expect(Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('')).toBe(golden)
    const decoded = PxMessage.toObject(PxMessage.decode(bytes))
    expect(decoded).toEqual(fields)
    expect(decoded.textInput).toBeUndefined()
  })

  it('carries ordinary input barrier generation independently of the controller lease', () => {
    const fields = { type: 50, inputGeneration: '9007199254740995', keyEvent: { keyCode: 87, down: true } }
    const decoded = PxMessage.toObject(PxMessage.decode(PxMessage.encode(PxMessage.create(fields)).finish()))
    expect(decoded).toEqual(fields)
  })
})
