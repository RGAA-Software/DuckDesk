import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  MSG_TYPE_SIG_ANSWER_SDP,
  MSG_TYPE_SIG_ICE,
  MSG_TYPE_SIG_OFFER_SDP,
  PxMessage,
  RelayMessage,
  RelayMessageType,
} from '../src/rtc/proto'
import { StandardRtcSignaling, type StandardRtcSignalParams } from '../src/rtc/standard_signaling'

const relayType = (name: string) => RelayMessageType.values[name]

class FakeWebSocket {
  static readonly OPEN = 1
  static instances: FakeWebSocket[] = []

  readonly url: string
  readyState = 0
  binaryType = ''
  sent: Uint8Array[] = []
  closeCount = 0
  onopen: (() => void) | null = null
  onmessage: ((event: { data: ArrayBuffer }) => void) | null = null
  onerror: (() => void) | null = null
  onclose: (() => void) | null = null

  constructor(url: string) {
    this.url = url
    FakeWebSocket.instances.push(this)
  }

  open() {
    this.readyState = FakeWebSocket.OPEN
    this.onopen?.()
  }

  send(data: Uint8Array) {
    this.sent.push(new Uint8Array(data))
  }

  close() {
    if (this.readyState === 3) return
    this.readyState = 3
    this.closeCount += 1
    this.onclose?.()
  }

  relay(value: Record<string, unknown>) {
    const bytes = RelayMessage.encode(RelayMessage.create(value)).finish()
    const data = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer
    this.onmessage?.({ data })
  }
}

const params: StandardRtcSignalParams = {
  relayHost: '10.0.0.1',
  relayPort: 20366,
  remoteDeviceId: 'device-90',
  ticketDeviceId: 'ticket-device-90',
  streamId: 'desktop',
  ticket: 'one-time-ticket',
  clientNonce: 'nonce-1',
  instanceId: 'instance-1',
  safetyPwdMd5: '',
  secure: false,
}

function createSignaling() {
  const remoteIce: RTCIceCandidateInit[] = []
  const logs: string[] = []
  const signaling = new StandardRtcSignaling(
    params,
    async (candidate) => { remoteIce.push(candidate) },
    (message) => logs.push(message),
  )
  return { signaling, remoteIce, logs }
}

async function connectReady(signaling: StandardRtcSignaling) {
  const connected = signaling.connect()
  const socket = FakeWebSocket.instances.at(-1)!
  socket.open()
  socket.relay({
    type: relayType('kRelayCreateRoomResp'),
    createRoomResp: { roomId: 'room-1' },
  })
  socket.relay({ type: relayType('kRelayRoomPrepared'), roomPrepared: { roomId: 'room-1' } })
  await connected
  return socket
}

function targetPayloads(socket: FakeWebSocket) {
  return socket.sent
    .map((bytes) => RelayMessage.decode(bytes) as unknown as Record<string, any>)
    .filter((message) => message.type === relayType('kRelayTargetMessage'))
    .map((message) => PxMessage.decode(message.relay.payload) as unknown as Record<string, any>)
}

beforeEach(() => {
  vi.useFakeTimers()
  FakeWebSocket.instances = []
  vi.stubGlobal('window', globalThis)
  vi.stubGlobal('WebSocket', FakeWebSocket)
})

afterEach(() => {
  vi.clearAllTimers()
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

describe('StandardRtcSignaling lifecycle', () => {
  it('authenticates the Relay URL and resolves only after the room is prepared', async () => {
    const { signaling, logs } = createSignaling()
    const connected = signaling.connect()
    const socket = FakeWebSocket.instances[0]
    const url = new URL(socket.url)
    expect(url.pathname).toBe('/relay')
    expect(url.searchParams.get('ticket')).toBe(params.ticket)
    expect(url.searchParams.get('client_nonce')).toBe(params.clientNonce)
    expect(url.searchParams.get('instance_id')).toBe(params.instanceId)

    let settled = false
    void connected.then(() => { settled = true })
    socket.open()
    await Promise.resolve()
    expect(settled).toBe(false)
    socket.relay({
      type: relayType('kRelayCreateRoomResp'),
      createRoomResp: { roomId: 'room-1' },
    })
    socket.relay({ type: relayType('kRelayRoomPrepared'), roomPrepared: { roomId: 'room-1' } })
    await connected
    expect(logs).toContain('[rtc-standard] signaling room ready: room-1')
    signaling.stop()
  })

  it('opens a password-authenticated guest Relay without ticket material', () => {
    const signaling = new StandardRtcSignaling(
      {
        ...params,
        ticket: '',
        clientNonce: '',
        instanceId: '',
        safetyPwdMd5: 'device-password-md5',
      },
      async () => {},
      () => {},
    )
    void signaling.connect().catch(() => {})
    const socket = FakeWebSocket.instances[0]
    const url = new URL(socket.url)
    expect(url.searchParams.get('guest_password')).toBe('1')
    expect(url.searchParams.get('ticket')).toBeNull()
    expect(url.searchParams.get('client_nonce')).toBeNull()
    expect(url.searchParams.get('instance_id')).toBeNull()
    signaling.stop()
  })

  it('closes the socket and clears timers on connection timeout', async () => {
    const { signaling, logs } = createSignaling()
    const connected = signaling.connect()
    const socket = FakeWebSocket.instances[0]
    socket.open()
    const rejected = expect(connected).rejects.toThrow('连接超时')

    await vi.advanceTimersByTimeAsync(10_000)
    await rejected
    expect(socket.closeCount).toBe(1)
    expect(vi.getTimerCount()).toBe(0)
    expect(logs.filter((line) => line.includes('连接超时'))).toHaveLength(1)

    socket.relay({ type: relayType('kRelayRoomPrepared'), roomPrepared: { roomId: 'late' } })
    expect(logs.some((line) => line.includes('late'))).toBe(false)
  })

  it('settles Relay errors once without an orphan heartbeat', async () => {
    const { signaling, logs } = createSignaling()
    const connected = signaling.connect()
    const socket = FakeWebSocket.instances[0]
    socket.open()
    const rejected = expect(connected).rejects.toThrow('rejected')
    socket.relay({
      type: relayType('kRelayError'),
      relayError: { message: 'rejected' },
    })

    await rejected
    expect(socket.closeCount).toBe(1)
    expect(vi.getTimerCount()).toBe(0)
    expect(logs.filter((line) => line.includes('rejected'))).toHaveLength(1)
  })

  it('rejects a pending connect immediately when stopped', async () => {
    const { signaling } = createSignaling()
    const connected = signaling.connect()
    const rejected = expect(connected).rejects.toThrow('已停止')
    signaling.stop()
    await rejected
    expect(vi.getTimerCount()).toBe(0)
  })
})

describe('StandardRtcSignaling negotiation', () => {
  it('flushes bounded local ICE after the offer in original order', async () => {
    const { signaling, logs } = createSignaling()
    for (let i = 0; i < 300; i += 1) {
      signaling.sendIce({ candidate: `candidate-${i}`, sdpMid: '0', sdpMLineIndex: 0 } as RTCIceCandidate)
    }
    const socket = await connectReady(signaling)
    const answer = signaling.exchangeOffer('offer-sdp', 'rotated-ticket', 'nonce-2', 'instance-2')
    const payloads = targetPayloads(socket)
    expect(payloads[0].type).toBe(MSG_TYPE_SIG_OFFER_SDP)
    const ice = payloads.filter((message) => message.type === MSG_TYPE_SIG_ICE)
    expect(ice).toHaveLength(256)
    expect(ice[0].sigIce.ice).toBe('candidate-44')
    expect(ice.at(-1).sigIce.ice).toBe('candidate-299')
    expect(logs.filter((line) => line.includes('queue full'))).toHaveLength(44)
    signaling.stop()
    await expect(answer).rejects.toThrow('已停止')
  })

  it('puts only the device password digest in a guest SDP offer', async () => {
    const signaling = new StandardRtcSignaling(
      { ...params, ticket: '', clientNonce: '', instanceId: '', safetyPwdMd5: 'guest-md5' },
      async () => {},
      () => {},
    )
    const socket = await connectReady(signaling)
    const answer = signaling.exchangeOffer('guest-offer', '', '', '', 'guest-md5')
    const offer = targetPayloads(socket).find((message) => message.type === MSG_TYPE_SIG_OFFER_SDP)
    expect(offer.sigOfferSdp.connectionTicket).toBe('')
    expect(offer.sigOfferSdp.clientNonce).toBe('')
    expect(offer.sigOfferSdp.safetyPwdMd5).toBe('guest-md5')
    signaling.stop()
    await expect(answer).rejects.toThrow('已停止')
  })

  it('rejects concurrent offers and resolves exactly one matching answer', async () => {
    const { signaling } = createSignaling()
    const socket = await connectReady(signaling)
    const first = signaling.exchangeOffer('offer-1', 'ticket-1', 'nonce-1', '')
    await expect(signaling.exchangeOffer('offer-2', 'ticket-2', 'nonce-2', '')).rejects.toThrow('正在进行')

    const px = PxMessage.encode(PxMessage.create({
      type: MSG_TYPE_SIG_ANSWER_SDP,
      sigAnswerSdp: { sdp: 'answer-1' },
    })).finish()
    socket.relay({
      type: relayType('kRelayTargetMessage'),
      relay: { roomIds: ['room-1'], payload: px },
    })
    await expect(first).resolves.toBe('answer-1')
    signaling.stop()
  })

  it('returns a structured occupied result without closing the Relay, so a confirmed takeover can retry', async () => {
    const { signaling } = createSignaling()
    const socket = await connectReady(signaling)
    const rejected = signaling.exchangeOffer('offer-1', 'ticket-1', 'nonce-1', '')
    const occupied = PxMessage.encode(PxMessage.create({
      type: MSG_TYPE_SIG_ANSWER_SDP,
      sigAnswerSdp: { errorCode: 'RTC_OCCUPIED' },
    })).finish()
    socket.relay({
      type: relayType('kRelayTargetMessage'),
      relay: { roomIds: ['room-1'], payload: occupied },
    })
    await expect(rejected).rejects.toThrow('RTC_OCCUPIED')
    expect(socket.closeCount).toBe(0)

    const accepted = signaling.exchangeOffer('offer-2', 'ticket-2', 'nonce-2', '', '', true)
    const offer = targetPayloads(socket).at(-1)
    expect(offer.sigOfferSdp.takeover).toBe(true)
    const answer = PxMessage.encode(PxMessage.create({
      type: MSG_TYPE_SIG_ANSWER_SDP,
      sigAnswerSdp: { sdp: 'answer-2' },
    })).finish()
    socket.relay({
      type: relayType('kRelayTargetMessage'),
      relay: { roomIds: ['room-1'], payload: answer },
    })
    await expect(accepted).resolves.toBe('answer-2')
    signaling.stop()
  })

  it('makes answer timeout terminal so a late answer cannot pollute a retry', async () => {
    const { signaling } = createSignaling()
    const socket = await connectReady(signaling)
    const first = signaling.exchangeOffer('offer-1', 'ticket-1', 'nonce-1', '')
    const rejected = expect(first).rejects.toThrow('Answer SDP 等待超时')
    await vi.advanceTimersByTimeAsync(15_000)
    await rejected
    expect(socket.closeCount).toBe(1)

    const px = PxMessage.encode(PxMessage.create({
      type: MSG_TYPE_SIG_ANSWER_SDP,
      sigAnswerSdp: { sdp: 'late-answer' },
    })).finish()
    socket.relay({
      type: relayType('kRelayTargetMessage'),
      relay: { roomIds: ['room-1'], payload: px },
    })
    await expect(signaling.exchangeOffer('offer-2', 'ticket-2', 'nonce-2', '')).rejects.toThrow('尚未准备')
  })
})
