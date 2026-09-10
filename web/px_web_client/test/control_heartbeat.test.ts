import { afterEach, describe, expect, it, vi } from 'vitest'
import { startControlHeartbeat } from '../src/rtc/control_heartbeat'
import { MessageType } from '../src/rtc/protocol_enums'

afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers() })

describe('reliable control heartbeat', () => {
  it('sends protocol heartbeats immediately and periodically, not diagnostic ping', () => {
    vi.useFakeTimers()
    const send = vi.fn(() => true)
    const stop = startControlHeartbeat(send)
    expect(send.mock.calls[0]).toEqual([{ type: MessageType.HeartBeat, heartbeat: { index: 0, timestamp: Date.now() } }])
    vi.advanceTimersByTime(16000)
    expect(send).toHaveBeenCalledTimes(9)
    stop()
    stop()
    vi.advanceTimersByTime(16000)
    expect(send).toHaveBeenCalledTimes(9)
  })

  it('ignores a queued timer after cleanup and starts a fresh reconnect sequence', () => {
    vi.useFakeTimers()
    const interval = vi.spyOn(globalThis, 'setInterval')
    const send = vi.fn(() => true)
    const stop = startControlHeartbeat(send)
    const queued = interval.mock.calls[0]![0] as () => void
    stop()
    queued()
    expect(send).toHaveBeenCalledTimes(1)
    const stopAgain = startControlHeartbeat(send)
    expect(send.mock.calls[1]).toEqual([{ type: MessageType.HeartBeat, heartbeat: { index: 0, timestamp: Date.now() } }])
    stopAgain()
  })

  it('does not spin or replay rejected sends', () => {
    vi.useFakeTimers()
    const send = vi.fn(() => false)
    const stop = startControlHeartbeat(send)
    vi.advanceTimersByTime(1999)
    expect(send).toHaveBeenCalledTimes(1)
    vi.advanceTimersByTime(1)
    expect(send).toHaveBeenCalledTimes(2)
    stop()
  })
})
