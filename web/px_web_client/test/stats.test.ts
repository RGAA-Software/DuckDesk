import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { PerfCollector, perfSummaryLine, type PerfStats } from '../src/rtc/stats'

type Stat = Record<string, unknown>

function report(...items: Stat[]) {
  return { forEach: (callback: (value: Stat) => void) => items.forEach(callback) } as RTCStatsReport
}

function peer(stats: RTCStatsReport) {
  return {
    connectionState: 'connected',
    getStats: vi.fn(async () => stats),
  } as unknown as RTCPeerConnection
}

beforeEach(() => {
  vi.useFakeTimers()
  vi.setSystemTime(new Date('2026-08-24T00:00:00Z'))
  vi.stubGlobal('window', globalThis)
})

afterEach(() => {
  vi.clearAllTimers()
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

describe('PerfCollector selected path diagnostics', () => {
  it('reports unknown candidates without throwing when no pair is selected', async () => {
    const updates: PerfStats[] = []
    const collector = new PerfCollector((value) => updates.push(value))
    collector.start(peer(report({
      id: 'video', type: 'inbound-rtp', kind: 'video', bytesReceived: 1,
    })))
    await vi.advanceTimersByTimeAsync(0)
    expect(updates).toHaveLength(1)
    expect(updates[0].localCand).toBe('?')
    expect(updates[0].remoteCand).toBe('?')
    expect(perfSummaryLine(updates[0])).toContain('path=? <-> ?')
    collector.stop()
  })

  it('shows relay/TCP transport separately from relayed UDP media', async () => {
    const updates: PerfStats[] = []
    const collector = new PerfCollector((value) => updates.push(value))
    collector.start(peer(report(
      { id: 'video', type: 'inbound-rtp', kind: 'video', bytesReceived: 1 },
      {
        id: 'pair', type: 'candidate-pair', nominated: true,
        currentRoundTripTime: 0.042, localCandidateId: 'local', remoteCandidateId: 'remote',
      },
      {
        id: 'local', type: 'local-candidate', candidateType: 'relay', address: '10.0.0.1',
        port: 50000, protocol: 'udp', relayProtocol: 'tcp',
      },
      {
        id: 'remote', type: 'remote-candidate', candidateType: 'relay', ip: '10.0.0.90',
        port: 50001, protocol: 'udp', relayProtocol: 'tcp',
      },
    )))
    await vi.advanceTimersByTimeAsync(0)
    expect(updates[0].rttMs).toBe(42)
    expect(updates[0].localCand).toBe('relay 10.0.0.1:50000/udp via turn:tcp')
    expect(updates[0].remoteCand).toBe('relay 10.0.0.90:50001/udp via turn:tcp')
    collector.stop()
  })
})
