import { afterEach, describe, expect, it, vi } from 'vitest'
import { InputController } from '../src/rtc/input'
import { TlvReassembler } from '../src/rtc/tlv'
import { decodeMessage } from '../src/rtc/proto'

function fixture() {
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
  const dc = { readyState: 'open', send: (bytes: ArrayBuffer) => {
    for (const payload of reassembler.feed(bytes)) messages.push(decodeMessage(payload))
  } }
  const controller = new InputController({
    dc: dc as unknown as RTCDataChannel, deviceId: 'device', streamId: 'stream', monitorName: 'monitor',
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
    const f = fixture()
    f.key('keydown')
    f.controller.setTextEditing(true)
    expect(f.messages.map(message => message.keyEvent.down)).toEqual([true, false])
    f.key('keydown', 'Space')
    f.key('keyup')
    expect(f.messages).toHaveLength(2)
    f.controller.setInputGeneration('9007199254740999')
    f.controller.setTextEditing(false)
    f.key('keydown')
    expect(f.messages.at(-1)!.inputGeneration).toBe('9007199254740999')
    f.controller.detach()
    expect(f.messages.at(-1)!.keyEvent.down).toBe(false)
  })

  it('never forwards composition keys and releases all tracked keys on real blur', () => {
    const f = fixture()
    f.key('keydown', 'KeyW', true)
    expect(f.messages).toHaveLength(0)
    f.key('keydown')
    f.key('keydown', 'Space')
    f.windowEvents.dispatchEvent(new Event('blur'))
    expect(f.messages.map(message => message.keyEvent.down)).toEqual([true, true, false, false])
    f.controller.detach()
  })

  it('permission loss releases held keys and repeated detach is harmless', () => {
    const f = fixture()
    f.key('keydown')
    f.controller.viewOnly = true
    f.key('keydown')
    expect(f.messages.map(message => message.keyEvent.down)).toEqual([true, false])
    f.controller.detach()
    f.controller.detach()
    f.key('keydown')
    expect(f.messages).toHaveLength(2)
    f.controller.attach()
    f.controller.viewOnly = false
    f.key('keydown')
    expect(f.messages).toHaveLength(3)
    f.controller.detach()
  })

  it('retains ordinary text commits outside the panel without duplicate local-editor input', () => {
    const f = fixture()
    f.sink.value = 'ordinary English'
    f.sink.dispatchEvent(new Event('input'))
    expect(f.messages.at(-1)!.textInput.text).toBe('ordinary English')
    f.controller.setTextEditing(true)
    const count = f.messages.length
    f.sink.value = '面板输入不得走旧通道'
    f.sink.dispatchEvent(new Event('input'))
    expect(f.messages).toHaveLength(count)
    f.controller.setInputGeneration('2')
    f.controller.setTextEditing(false)
    f.sink.value = 'English after panel'
    f.sink.dispatchEvent(new Event('input'))
    expect(f.messages.at(-1)!.inputGeneration).toBe('2')
    expect(f.messages.at(-1)!.textInput.text).toBe('English after panel')
    f.controller.detach()
  })
})
