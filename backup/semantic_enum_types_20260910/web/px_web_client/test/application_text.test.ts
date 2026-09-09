import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApplicationTextTransport, reliableApplicationControlChannel } from '../src/rtc/application_text'
import { TextInputWorkflow } from '../src/rtc/text_input_workflow'

const target = { instanceId: 'app-a', leaseGeneration: '9007199254740993', targetGeneration: '2' }
function fixture() {
  const workflow = new TextInputWorkflow()
  const sent: Array<Record<string, any>> = []
  const suspend = vi.fn()
  const changed = vi.fn()
  const carrier = { ready: true, send: vi.fn((fields: Record<string, unknown>) => { sent.push(fields); return true }) }
  const adapter = new ApplicationTextTransport({
    workflow, instanceId: 'app-a', suspend, changed, send: carrier.send, canSend: () => carrier.ready,
  })
  adapter.start()
  adapter.receive({ type: 610, applicationTextCapabilities: {
    version: 1, maxUtf8Bytes: 16384, finalTextSupported: true, stateHintsSupported: true,
    parentBindingId: 'binding', inputGeneration: '0',
  } })
  adapter.receive({ type: 611, applicationTextState: { target, editability: 1 } })
  const acknowledge = (editing: boolean, generation: string) => adapter.receive({
    type: 615, applicationTextBarrierResult: {
      requestId: sent.at(-1)!.applicationTextBarrier.requestId, target, outcome: 2, inputGeneration: generation, editing,
    },
  })
  return { workflow, sent, suspend, changed, carrier, adapter, acknowledge }
}

afterEach(() => vi.useRealTimers())

describe('reliable application text adapter', () => {
  it('accepts only the existing fully reliable ordered control channel', () => {
    const channel = { readyState: 'open' as RTCDataChannelState, ordered: true, maxRetransmits: null, maxPacketLifeTime: null }
    expect(reliableApplicationControlChannel(channel)).toBe(true)
    expect(reliableApplicationControlChannel({ ...channel, ordered: false })).toBe(false)
    expect(reliableApplicationControlChannel({ ...channel, maxRetransmits: 0 })).toBe(false)
    expect(reliableApplicationControlChannel({ ...channel, maxPacketLifeTime: 500 })).toBe(false)
    expect(reliableApplicationControlChannel({ ...channel, readyState: 'closed' })).toBe(false)
    expect(reliableApplicationControlChannel(null)).toBe(false)
  })

  it('suppresses control before begin and enables sending only after matching barrier', () => {
    const f = fixture()
    expect(f.sent[0].type).toBe(610)
    expect(f.adapter.beginEditing()).toBe(true)
    expect(f.suspend).toHaveBeenLastCalledWith(true, '0')
    expect(f.workflow.open()).toBe(false)
    expect(f.adapter.beginEditing()).toBe(false)
    f.acknowledge(true, '1')
    expect(f.workflow.isOpen).toBe(true)
    f.workflow.edit('中文😀\n')
    const submission = f.workflow.begin('request_1')!
    f.adapter.submit(submission)
    expect(f.sent.at(-1)!.applicationTextSubmit.text).toBe('中文😀\n')
    expect(f.sent.at(-1)!.applicationTextSubmit.inputGeneration).toBe('1')
    f.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_1', target, outcome: 1 } })
    expect(f.workflow.canSend).toBe(false)
    f.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_1', target, outcome: 2 } })
    expect(f.workflow.draft).toBe('')
    expect(f.adapter.endEditing()).toBe(true)
    expect(f.suspend).toHaveBeenLastCalledWith(true, '1')
    f.acknowledge(false, '2')
    expect(f.suspend).toHaveBeenLastCalledWith(false, '2')
    f.adapter.dispose()
  })

  it('closes safely while begin acknowledgement is queued', () => {
    const f = fixture()
    f.adapter.beginEditing()
    f.adapter.endEditing()
    f.acknowledge(true, '1')
    expect(f.sent.at(-1)!.applicationTextBarrier.beginEditing).toBe(false)
    expect(f.workflow.isOpen).toBe(false)
    f.acknowledge(false, '2')
    expect(f.suspend).toHaveBeenLastCalledWith(false, '2')
    f.adapter.dispose()
  })

  it('preserves early draft and composition while the begin barrier is pending', () => {
    const f = fixture()
    f.adapter.beginEditing()
    f.workflow.edit('屏障之前正在输入')
    f.workflow.setComposing(true)
    f.acknowledge(true, '1')
    expect(f.workflow.draft).toBe('屏障之前正在输入')
    expect(f.workflow.isComposing).toBe(true)
    expect(f.workflow.canSend).toBe(false)
    f.adapter.dispose()
  })

  it('does not resume ordinary input while a submitted backend operation remains pending', () => {
    const f = fixture()
    f.adapter.beginEditing()
    f.acknowledge(true, '1')
    f.workflow.edit('等待结果')
    f.adapter.submit(f.workflow.begin('request_4')!)
    const count = f.sent.length
    f.adapter.endEditing()
    expect(f.sent).toHaveLength(count)
    f.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_4', target, outcome: 2 } })
    expect(f.sent.at(-1)!.applicationTextBarrier.beginEditing).toBe(false)
    f.acknowledge(false, '2')
    expect(f.suspend).toHaveBeenLastCalledWith(false, '2')
    f.adapter.dispose()
  })

  it('keeps generation fenced after barrier timeout and never resends', () => {
    vi.useFakeTimers()
    const f = fixture()
    f.adapter.beginEditing()
    const count = f.sent.length
    vi.advanceTimersByTime(10001)
    expect(f.suspend).toHaveBeenLastCalledWith(true, '0')
    expect(f.changed.mock.lastCall?.[0]).toBe(false)
    expect(f.sent).toHaveLength(count)
    expect(f.carrier.ready).toBe(true)
    f.adapter.dispose()
  })

  it('does not let state hints cancel an outstanding submission deadline', () => {
    vi.useFakeTimers()
    const f = fixture()
    f.adapter.beginEditing()
    f.acknowledge(true, '1')
    f.workflow.edit('不重发')
    f.adapter.submit(f.workflow.begin('request_2')!)
    vi.advanceTimersByTime(5000)
    f.adapter.receive({ type: 611, applicationTextState: { target, editability: 1 } })
    vi.advanceTimersByTime(5001)
    expect(f.workflow.outcome).toBe('outcome_unknown')
    expect(f.workflow.draft).toBe('不重发')
    f.adapter.dispose()
  })

  it('rejects crossed instance or stale lease replies and preserves the draft', () => {
    const f = fixture()
    f.adapter.beginEditing()
    f.acknowledge(true, '1')
    f.workflow.edit('保留')
    f.adapter.submit(f.workflow.begin('request_3')!)
    for (const bad of [{ ...target, instanceId: 'app-b' }, { ...target, leaseGeneration: '1' }]) {
      f.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_3', target: bad, outcome: 2 } })
    }
    expect(f.workflow.draft).toBe('保留')
    expect(f.workflow.canSend).toBe(false)
    f.adapter.dispose()
    expect(f.workflow.outcome).toBe('outcome_unknown')
  })

  it('does not process callbacks after disposal; stop and dispose are repeatable', () => {
    const f = fixture()
    f.adapter.dispose()
    const count = f.changed.mock.calls.length
    f.adapter.receive({ type: 611, applicationTextState: { target, editability: 1 } })
    f.adapter.dispose()
    expect(f.changed).toHaveBeenCalledTimes(count)
    expect(f.carrier.ready).toBe(true)
  })

  it('immediately marks an outstanding submission uncertain when the existing carrier disconnects', () => {
    const f = fixture()
    f.adapter.beginEditing()
    f.acknowledge(true, '1')
    f.workflow.edit('断线保留')
    f.adapter.submit(f.workflow.begin('request_disconnect')!)
    f.carrier.ready = false
    f.adapter.disconnected()
    expect(f.workflow.outcome).toBe('outcome_unknown')
    expect(f.workflow.draft).toBe('断线保留')
    expect(f.workflow.canSend).toBe(false)
    f.adapter.dispose()
  })

  it('accepts bounded opaque game target identities while preserving decimal lease/input generations', () => {
    const f = fixture()
    const gameTarget = { ...target, targetGeneration: '1824:134020690047656780:65584:7' }
    f.adapter.receive({ type: 611, applicationTextState: { target: gameTarget, editability: 0 } })
    expect(f.adapter.beginEditing()).toBe(true)
    const barrier = f.sent.at(-1)!.applicationTextBarrier
    expect(barrier.target.targetGeneration).toBe(gameTarget.targetGeneration)
    f.adapter.receive({ type: 615, applicationTextBarrierResult: {
      requestId: barrier.requestId, target: gameTarget, outcome: 2, inputGeneration: '1', editing: true,
    } })
    f.workflow.edit('游戏中文')
    expect(f.workflow.canSend).toBe(true)
    expect(f.workflow.begin('game_request')!.target.targetGeneration).toBe(gameTarget.targetGeneration)
    f.adapter.dispose()
  })

  it('polls lightweight hints while idle/editing but never during an outstanding barrier', () => {
    vi.useFakeTimers()
    const f = fixture()
    const initial = f.sent.length
    vi.advanceTimersByTime(750)
    expect(f.sent.length).toBe(initial + 1)
    expect(f.sent.at(-1)!.type).toBe(610)
    f.adapter.beginEditing()
    const barrier = f.sent.length
    vi.advanceTimersByTime(750)
    expect(f.sent.length).toBe(barrier)
    f.acknowledge(true, '1')
    const editing = f.sent.length
    vi.advanceTimersByTime(1500)
    expect(f.sent.length).toBe(editing + 2)
    f.adapter.dispose()
    vi.advanceTimersByTime(1500)
    expect(f.sent.length).toBe(editing + 2)
  })
})
