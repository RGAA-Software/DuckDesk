import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApplicationTextTransport, reliableApplicationControlChannel } from '../src/rtc/application_text'
import { TextInputWorkflow } from '../src/rtc/text_input_workflow'
import { MessageType, TextEditability, TextOutcomeCode } from '../src/rtc/protocol_enums'

const target = { instanceId: 'app-a', leaseGeneration: '9007199254740993', targetGeneration: '2' }
function createApplicationTextFixture() {
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
  it.each([TextOutcomeCode.TargetChanged, TextOutcomeCode.TargetUnavailable, TextOutcomeCode.Busy])(
    'recovers only after explicitly closing a definitively rejected begin (%s)', outcome => {
      vi.useFakeTimers()
      const testFixture = createApplicationTextFixture()
      testFixture.adapter.beginEditing()
      testFixture.workflow.edit('保留草稿')
      testFixture.adapter.receive({ type: MessageType.ApplicationTextBarrierResult, applicationTextBarrierResult: {
        requestId: testFixture.sent.at(-1)!.applicationTextBarrier.requestId, target, outcome, inputGeneration: '0', editing: false,
      } })
      expect(testFixture.workflow.draft).toBe('保留草稿')
      expect(testFixture.adapter.beginEditing()).toBe(false)
      const nextTarget = { ...target, targetGeneration: '3' }
      testFixture.adapter.receive({ type: MessageType.ApplicationTextState,
        applicationTextState: { target: nextTarget, editability: TextEditability.Unknown } })
      expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '0')
      const messageCount = testFixture.sent.length
      expect(testFixture.adapter.endEditing()).toBe(true)
      expect(testFixture.sent).toHaveLength(messageCount)
      expect(testFixture.suspend).toHaveBeenLastCalledWith(false, '0')
      expect(testFixture.adapter.beginEditing()).toBe(true)
      expect(testFixture.sent.at(-1)!.applicationTextBarrier.target).toEqual(nextTarget)
      testFixture.adapter.dispose()
    })

  it('keeps uncertain or changed-generation rejection fenced even after close', () => {
    for (const [outcome, generation] of [[TextOutcomeCode.Unknown, '0'], [TextOutcomeCode.TargetChanged, '1']] as const) {
      const testFixture = createApplicationTextFixture()
      testFixture.adapter.beginEditing()
      testFixture.adapter.receive({ type: MessageType.ApplicationTextBarrierResult, applicationTextBarrierResult: {
        requestId: testFixture.sent.at(-1)!.applicationTextBarrier.requestId, target, outcome, inputGeneration: generation, editing: false,
      } })
      expect(testFixture.adapter.endEditing()).toBe(false)
      expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '0')
      testFixture.adapter.dispose()
    }
  })

  it('honors a queued close after a definitive begin rejection', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.adapter.endEditing()
    const messageCount = testFixture.sent.length
    testFixture.adapter.receive({ type: MessageType.ApplicationTextBarrierResult, applicationTextBarrierResult: {
      requestId: testFixture.sent.at(-1)!.applicationTextBarrier.requestId, target, outcome: TextOutcomeCode.TargetChanged,
      inputGeneration: '0', editing: false,
    } })
    expect(testFixture.sent).toHaveLength(messageCount)
    expect(testFixture.suspend).toHaveBeenLastCalledWith(false, '0')
    testFixture.adapter.dispose()
  })

  it('revokes local rejection recovery when a later lease changes', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.adapter.receive({ type: MessageType.ApplicationTextBarrierResult, applicationTextBarrierResult: {
      requestId: testFixture.sent.at(-1)!.applicationTextBarrier.requestId, target, outcome: TextOutcomeCode.TargetChanged,
      inputGeneration: '0', editing: false,
    } })
    testFixture.adapter.receive({ type: MessageType.ApplicationTextState,
      applicationTextState: { target: { ...target, leaseGeneration: '4' }, editability: TextEditability.Unknown } })
    expect(testFixture.adapter.endEditing()).toBe(false)
    expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '0')
    testFixture.adapter.dispose()
  })

  it('treats unknown wire editability as unknown rather than editable', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.receive({ type: MessageType.ApplicationTextState,
      applicationTextState: { target, editability: 99 as TextEditability } })
    expect(testFixture.changed).toHaveBeenLastCalledWith(true, false, '')
    testFixture.adapter.dispose()
  })

  it('rejects an unknown barrier outcome without enabling editing', () => {
    const testFixture = createApplicationTextFixture()
    expect(testFixture.adapter.beginEditing()).toBe(true)
    testFixture.adapter.receive({ type: MessageType.ApplicationTextBarrierResult, applicationTextBarrierResult: {
      requestId: testFixture.sent.at(-1)!.applicationTextBarrier.requestId, target, outcome: 99 as TextOutcomeCode,
      inputGeneration: '1', editing: true,
    } })
    expect(testFixture.suspend).not.toHaveBeenCalledWith(true, '1')
    expect(testFixture.changed).toHaveBeenLastCalledWith(false, false, expect.any(String))
    testFixture.adapter.dispose()
  })

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
    const testFixture = createApplicationTextFixture()
    expect(testFixture.sent[0].type).toBe(610)
    expect(testFixture.adapter.beginEditing()).toBe(true)
    expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '0')
    expect(testFixture.workflow.open()).toBe(false)
    expect(testFixture.adapter.beginEditing()).toBe(false)
    testFixture.acknowledge(true, '1')
    expect(testFixture.workflow.isOpen).toBe(true)
    testFixture.workflow.edit('中文😀\n')
    const submission = testFixture.workflow.begin('request_1')!
    testFixture.adapter.submit(submission)
    expect(testFixture.sent.at(-1)!.applicationTextSubmit.text).toBe('中文😀\n')
    expect(testFixture.sent.at(-1)!.applicationTextSubmit.inputGeneration).toBe('1')
    testFixture.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_1', target, outcome: 1 } })
    expect(testFixture.workflow.canSend).toBe(false)
    testFixture.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_1', target, outcome: 2 } })
    expect(testFixture.workflow.draft).toBe('')
    expect(testFixture.adapter.endEditing()).toBe(true)
    expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '1')
    testFixture.acknowledge(false, '2')
    expect(testFixture.suspend).toHaveBeenLastCalledWith(false, '2')
    testFixture.adapter.dispose()
  })

  it('closes safely while begin acknowledgement is queued', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.adapter.endEditing()
    testFixture.acknowledge(true, '1')
    expect(testFixture.sent.at(-1)!.applicationTextBarrier.beginEditing).toBe(false)
    expect(testFixture.workflow.isOpen).toBe(false)
    testFixture.acknowledge(false, '2')
    expect(testFixture.suspend).toHaveBeenLastCalledWith(false, '2')
    testFixture.adapter.dispose()
  })

  it('preserves early draft and composition while the begin barrier is pending', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.workflow.edit('屏障之前正在输入')
    testFixture.workflow.setComposing(true)
    testFixture.acknowledge(true, '1')
    expect(testFixture.workflow.draft).toBe('屏障之前正在输入')
    expect(testFixture.workflow.isComposing).toBe(true)
    expect(testFixture.workflow.canSend).toBe(false)
    testFixture.adapter.dispose()
  })

  it('does not resume ordinary input while a submitted backend operation remains pending', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.acknowledge(true, '1')
    testFixture.workflow.edit('等待结果')
    testFixture.adapter.submit(testFixture.workflow.begin('request_4')!)
    const messageCount = testFixture.sent.length
    testFixture.adapter.endEditing()
    expect(testFixture.sent).toHaveLength(messageCount)
    testFixture.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_4', target, outcome: 2 } })
    expect(testFixture.sent.at(-1)!.applicationTextBarrier.beginEditing).toBe(false)
    testFixture.acknowledge(false, '2')
    expect(testFixture.suspend).toHaveBeenLastCalledWith(false, '2')
    testFixture.adapter.dispose()
  })

  it('keeps generation fenced after barrier timeout and never resends', () => {
    vi.useFakeTimers()
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    const messageCount = testFixture.sent.length
    vi.advanceTimersByTime(10001)
    expect(testFixture.suspend).toHaveBeenLastCalledWith(true, '0')
    expect(testFixture.changed.mock.lastCall?.[0]).toBe(false)
    expect(testFixture.sent).toHaveLength(messageCount)
    expect(testFixture.carrier.ready).toBe(true)
    testFixture.adapter.dispose()
  })

  it('does not let state hints cancel an outstanding submission deadline', () => {
    vi.useFakeTimers()
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.acknowledge(true, '1')
    testFixture.workflow.edit('不重发')
    testFixture.adapter.submit(testFixture.workflow.begin('request_2')!)
    vi.advanceTimersByTime(5000)
    testFixture.adapter.receive({ type: 611, applicationTextState: { target, editability: 1 } })
    vi.advanceTimersByTime(5001)
    expect(testFixture.workflow.outcome).toBe('outcome_unknown')
    expect(testFixture.workflow.draft).toBe('不重发')
    testFixture.adapter.dispose()
  })

  it('rejects crossed instance or stale lease replies and preserves the draft', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.acknowledge(true, '1')
    testFixture.workflow.edit('保留')
    testFixture.adapter.submit(testFixture.workflow.begin('request_3')!)
    for (const invalidTarget of [{ ...target, instanceId: 'app-b' }, { ...target, leaseGeneration: '1' }]) {
      testFixture.adapter.receive({ type: 613, applicationTextResult: { requestId: 'request_3', target: invalidTarget, outcome: 2 } })
    }
    expect(testFixture.workflow.draft).toBe('保留')
    expect(testFixture.workflow.canSend).toBe(false)
    testFixture.adapter.dispose()
    expect(testFixture.workflow.outcome).toBe('outcome_unknown')
  })

  it('does not process callbacks after disposal; stop and dispose are repeatable', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.dispose()
    const callbackCount = testFixture.changed.mock.calls.length
    testFixture.adapter.receive({ type: 611, applicationTextState: { target, editability: 1 } })
    testFixture.adapter.dispose()
    expect(testFixture.changed).toHaveBeenCalledTimes(callbackCount)
    expect(testFixture.carrier.ready).toBe(true)
  })

  it('immediately marks an outstanding submission uncertain when the existing carrier disconnects', () => {
    const testFixture = createApplicationTextFixture()
    testFixture.adapter.beginEditing()
    testFixture.acknowledge(true, '1')
    testFixture.workflow.edit('断线保留')
    testFixture.adapter.submit(testFixture.workflow.begin('request_disconnect')!)
    testFixture.carrier.ready = false
    testFixture.adapter.disconnected()
    expect(testFixture.workflow.outcome).toBe('outcome_unknown')
    expect(testFixture.workflow.draft).toBe('断线保留')
    expect(testFixture.workflow.canSend).toBe(false)
    testFixture.adapter.dispose()
  })

  it('accepts bounded opaque game target identities while preserving decimal lease/input generations', () => {
    const testFixture = createApplicationTextFixture()
    const gameTarget = { ...target, targetGeneration: '1824:134020690047656780:65584:7' }
    testFixture.adapter.receive({ type: 611, applicationTextState: { target: gameTarget, editability: 0 } })
    expect(testFixture.adapter.beginEditing()).toBe(true)
    const barrier = testFixture.sent.at(-1)!.applicationTextBarrier
    expect(barrier.target.targetGeneration).toBe(gameTarget.targetGeneration)
    testFixture.adapter.receive({ type: 615, applicationTextBarrierResult: {
      requestId: barrier.requestId, target: gameTarget, outcome: 2, inputGeneration: '1', editing: true,
    } })
    testFixture.workflow.edit('游戏中文')
    expect(testFixture.workflow.canSend).toBe(true)
    expect(testFixture.workflow.begin('game_request')!.target.targetGeneration).toBe(gameTarget.targetGeneration)
    testFixture.adapter.dispose()
  })

  it('polls lightweight hints while idle/editing but never during an outstanding barrier', () => {
    vi.useFakeTimers()
    const testFixture = createApplicationTextFixture()
    const initialMessageCount = testFixture.sent.length
    vi.advanceTimersByTime(750)
    expect(testFixture.sent.length).toBe(initialMessageCount + 1)
    expect(testFixture.sent.at(-1)!.type).toBe(610)
    testFixture.adapter.beginEditing()
    const barrierMessageCount = testFixture.sent.length
    vi.advanceTimersByTime(750)
    expect(testFixture.sent.length).toBe(barrierMessageCount)
    testFixture.acknowledge(true, '1')
    const editingMessageCount = testFixture.sent.length
    vi.advanceTimersByTime(1500)
    expect(testFixture.sent.length).toBe(editingMessageCount + 2)
    testFixture.adapter.dispose()
    vi.advanceTimersByTime(1500)
    expect(testFixture.sent.length).toBe(editingMessageCount + 2)
  })
})
