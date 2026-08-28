import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { FileTransferClient, type FtJob } from '../src/rtc/file_transfer'
import {
  decodeMessage,
  encodeMessage,
  MSG_TYPE_FILE_ACTION,
} from '../src/rtc/proto'
import { packTlv, unpackTlv } from '../src/rtc/tlv'

beforeEach(() => {
  vi.useFakeTimers()
  vi.stubGlobal('window', globalThis)
})

afterEach(() => {
  vi.clearAllTimers()
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

describe('FileTransferClient await ordering', () => {
  it('does not lose a send_confirm delivered synchronously from RTCDataChannel.send', async () => {
    let client: FileTransferClient
    let inboundIndex = 0n
    let latestJobs: FtJob[] = []

    const dc = {
      readyState: 'open',
      bufferedAmount: 0,
      send: vi.fn((packet: ArrayBuffer) => {
        const tlv = unpackTlv(packet)
        expect(tlv).not.toBeNull()
        const message = decodeMessage(tlv!.payload) as unknown as {
          fileResponse?: { digest?: { id: number; fileNum: number } }
        }
        const digest = message.fileResponse?.digest
        if (!digest) return

        // Deliberately re-enter the client before send() returns. This models
        // the fast LAN/loopback response that previously exposed a lost wakeup.
        const confirm = encodeMessage({
          type: MSG_TYPE_FILE_ACTION,
          fileAction: {
            sendConfirm: { id: digest.id, fileNum: digest.fileNum, offsetBlk: 0 },
          },
        })
        client.handleChannelMessage(packTlv(confirm, inboundIndex++))
      }),
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      bufferedAmountLowThreshold: 0,
    } as unknown as RTCDataChannel

    client = new FileTransferClient({
      dc,
      deviceId: 'device-90',
      streamId: 'web_device-90',
      onJobsChanged: (jobs) => { latestJobs = jobs },
    })

    const file = new File(['Pixels immediate confirm'], 'instant.txt', {
      type: 'text/plain',
      lastModified: 1_700_000_000_000,
    })
    const job = client.upload([{
      name: '',
      file,
      size: file.size,
      modifiedTime: Math.floor(file.lastModified / 1000),
    }], 'C:/Users/Public/Documents/instant.txt', 'instant.txt')

    await vi.waitFor(() => {
      expect(latestJobs.find((candidate) => candidate.id === job.id)?.state).toBe('done')
    })
    expect(dc.send).toHaveBeenCalled()
    client.failAll('test complete')
  })
})
