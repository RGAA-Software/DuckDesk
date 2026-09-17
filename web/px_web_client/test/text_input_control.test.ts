import { afterEach, describe, expect, it, vi } from 'vitest'
import { InputController } from '../src/rtc/input'
import { TlvReassembler } from '../src/rtc/tlv'
import { decodeMessage } from '../src/rtc/proto'

function createInputFixture() {
  const windowEvents = new EventTarget()
  vi.stubGlobal('window', windowEvents)
  const video = Object.assign(new EventTarget(), {
    tabIndex: 0, videoWidth: 800, videoHeight: 600,
    contains: () => false,
    getBoundingClientRect: () => ({ x: 0, y: 0, width: 800, height: 600, left: 0, top: 0, right: 800, bottom: 600 }),
  })
  const sink = Object.assign(new EventTarget(), {
    style: {}, value: '', tabIndex: -1, setAttribute: vi.fn(), remove: vi.fn(), focus: vi.fn(),
  })
  const documentState = { activeElement: video, createElement: () => sink, body: { appendChild: vi.fn() } }
  vi.stubGlobal('document', documentState)
  const messages: Array<Record<string, any>> = []
  const reassembler = new TlvReassembler()
  const dataChannel = { readyState: 'open', send: (bytes: ArrayBuffer) => {
    for (const payload of reassembler.feed(bytes)) messages.push(decodeMessage(payload))
  } }
  const controller = new InputController({
    dc: dataChannel as unknown as RTCDataChannel, deviceId: 'device', streamId: 'stream', monitorName: 'monitor',
    video: video as unknown as HTMLVideoElement,
  })
  controller.setApplicationTextEnabled(true)
  controller.attach()
  const key = (type: string, code = 'KeyW', composing = false) => {
    const event = Object.assign(new Event(type, { cancelable: true }), {
      code, key: 'w', isComposing: composing, keyCode: composing ? 229 : 87,
      ctrlKey: false, altKey: false, metaKey: false, getModifierState: () => false,
    })
    windowEvents.dispatchEvent(event)
  }
  return { controller, messages, key, windowEvents, documentState, sink }
}

afterEach(() => vi.unstubAllGlobals())

describe('ordinary input fencing', () => {
  it('releases a held nonmodifier before editing and stamps acknowledged generations', () => {
    const testFixture = createInputFixture()
    testFixture.key('keydown')
    testFixture.controller.setTextEditing(true)
    expect(testFixture.messages.map(message => message.keyEvent.down)).toEqual([true, false])
    testFixture.key('keydown', 'Space')
    testFixture.key('keyup')
    expect(testFixture.messages).toHaveLength(2)
    testFixture.controller.setInputGeneration('9007199254740999')
    testFixture.controller.setTextEditing(false)
    testFixture.key('keydown')
    expect(testFixture.messages.at(-1)!.inputGeneration).toBe('9007199254740999')
    testFixture.controller.detach()
    expect(testFixture.messages.at(-1)!.keyEvent.down).toBe(false)
  })

  it('never forwards composition keys and releases all tracked keys on real blur', () => {
    const testFixture = createInputFixture()
    testFixture.key('keydown', 'KeyW', true)
    expect(testFixture.messages).toHaveLength(0)
    testFixture.key('keydown')
    testFixture.key('keydown', 'Space')
    testFixture.windowEvents.dispatchEvent(new Event('blur'))
    expect(testFixture.messages.map(message => message.keyEvent.down)).toEqual([true, true, false, false])
    testFixture.controller.detach()
  })

  it('permission loss releases held keys and repeated detach is harmless', () => {
    const testFixture = createInputFixture()
    testFixture.key('keydown')
    testFixture.controller.viewOnly = true
    testFixture.key('keydown')
    expect(testFixture.messages.map(message => message.keyEvent.down)).toEqual([true, false])
    testFixture.controller.detach()
    testFixture.controller.detach()
    testFixture.key('keydown')
    expect(testFixture.messages).toHaveLength(2)
    testFixture.controller.attach()
    testFixture.controller.viewOnly = false
    testFixture.key('keydown')
    expect(testFixture.messages).toHaveLength(3)
    testFixture.controller.detach()
  })

  it('retains ordinary text commits outside the panel without duplicate local-editor input', () => {
    const testFixture = createInputFixture()
    testFixture.sink.value = 'ordinary English'
    testFixture.sink.dispatchEvent(new Event('input'))
    expect(testFixture.messages.at(-1)!.textInput.text).toBe('ordinary English')
    testFixture.controller.setTextEditing(true)
    const messageCount = testFixture.messages.length
    testFixture.sink.value = '面板输入不得走旧通道'
    testFixture.sink.dispatchEvent(new Event('input'))
    expect(testFixture.messages).toHaveLength(messageCount)
    testFixture.controller.setInputGeneration('2')
    testFixture.controller.setTextEditing(false)
    testFixture.sink.value = 'English after panel'
    testFixture.sink.dispatchEvent(new Event('input'))
    expect(testFixture.messages.at(-1)!.inputGeneration).toBe('2')
    expect(testFixture.messages.at(-1)!.textInput.text).toBe('English after panel')
    testFixture.controller.detach()
  })
})
