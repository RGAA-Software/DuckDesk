import {
  MSG_TYPE_SIG_ANSWER_SDP,
  MSG_TYPE_SIG_ICE,
  MSG_TYPE_SIG_OFFER_SDP,
  PxMessage,
  RelayMessage,
  RelayMessageType,
} from './proto'

export interface StandardRtcSignalParams {
  relayHost: string
  relayPort: number
  remoteDeviceId: string
  ticketDeviceId: string
  streamId: string
  ticket: string
  clientNonce: string
  instanceId: string
  /** Guest sessions authenticate at Render with the device password digest. */
  safetyPwdMd5: string
  secure: boolean
}

type PendingAnswer = {
  resolve: (sdp: string) => void
  reject: (error: Error) => void
  timer: number
}

const typeValue = (name: string): number => RelayMessageType.values[name]
const MAX_PENDING_LOCAL_ICE = 256

function relayClientId(): string {
  if (globalThis.crypto?.randomUUID) {
    return `web_${globalThis.crypto.randomUUID().replace(/-/g, '')}`
  }
  const bytes = new Uint8Array(16)
  if (globalThis.crypto?.getRandomValues) globalThis.crypto.getRandomValues(bytes)
  else for (let i = 0; i < bytes.length; i += 1) bytes[i] = Math.floor(Math.random() * 256)
  return `web_${Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join('')}`
}

/** Console application-Relay signaling for standard WebRTC.
 *
 * Logged-in sessions authenticate the Relay with a short-lived ticket and the
 * Render atomically redeems it with the SDP offer. Guest sessions carry no
 * ticket: the Relay only scopes the signaling target and Render validates the
 * device password digest in the SDP offer. Media, input and file payloads never
 * use this socket after the PeerConnection is up.
 */
export class StandardRtcSignaling {
  private socket: WebSocket | null = null
  private roomId = ''
  private relayIndex = 0
  private heartbeatIndex = 0
  private heartbeatTimer: number | null = null
  private readonly clientId = relayClientId()
  private readyResolve: (() => void) | null = null
  private readyReject: ((error: Error) => void) | null = null
  private pendingAnswer: PendingAnswer | null = null
  private pendingLocalIce: RTCIceCandidate[] = []
  private canSendIce = false
  private closed = false

  constructor(
    private params: StandardRtcSignalParams,
    private onRemoteIce: (candidate: RTCIceCandidateInit) => Promise<void>,
    private onLog: (message: string) => void,
  ) {}

  async connect(): Promise<void> {
    if (this.socket) this.stop()
    const scheme = this.params.secure ? 'wss' : 'ws'
    const query = new URLSearchParams({
      device_id: this.clientId,
      remote_device_id: this.params.remoteDeviceId,
      ticket_device_id: this.params.ticketDeviceId,
      device_name: 'WebClient',
      stream_id: this.params.streamId,
      rtc_signal: '1',
    })
    if (this.params.ticket) {
      query.set('ticket', this.params.ticket)
      query.set('client_nonce', this.params.clientNonce)
      if (this.params.instanceId) query.set('instance_id', this.params.instanceId)
    } else {
      query.set('guest_password', '1')
    }
    const url = `${scheme}://${this.params.relayHost}:${this.params.relayPort}/relay?${query}`
    this.closed = false
    await new Promise<void>((resolve, reject) => {
      const timer = window.setTimeout(
        () => this.fail(new Error('标准 RTC 信令 Relay 连接超时')),
        10000,
      )
      this.readyResolve = () => {
        window.clearTimeout(timer)
        resolve()
      }
      this.readyReject = (error) => {
        window.clearTimeout(timer)
        reject(error)
      }
      const socket = new WebSocket(url)
      this.socket = socket
      socket.binaryType = 'arraybuffer'
      socket.onopen = () => {
        this.sendRelay({ type: typeValue('kRelayHello'), fromDeviceId: this.clientId, hello: {} })
        this.sendRelay({
          type: typeValue('kRelayCreateRoom'),
          fromDeviceId: this.clientId,
          createRoom: {
            deviceId: this.clientId,
            remoteDeviceId: this.params.remoteDeviceId,
            deviceName: 'WebClient',
            streamId: this.params.streamId,
          },
        })
        this.heartbeatTimer = window.setInterval(() => this.sendRelay({
          type: typeValue('kRelayHeartBeat'),
          fromDeviceId: this.clientId,
          heartbeat: { index: this.heartbeatIndex++ },
        }), 1000)
      }
      socket.onmessage = (event) => void this.onRelayMessage(event.data)
      socket.onerror = () => this.fail(new Error('标准 RTC 信令 Relay 连接失败'))
      socket.onclose = () => {
        if (this.heartbeatTimer !== null) window.clearInterval(this.heartbeatTimer)
        this.heartbeatTimer = null
        if (!this.closed) this.fail(new Error('标准 RTC 信令 Relay 已断开'))
      }
    })
  }

  async exchangeOffer(
    sdp: string,
    ticket: string,
    clientNonce: string,
    instanceId: string,
    safetyPwdMd5 = '',
  ): Promise<string> {
    if (!this.roomId) throw new Error('标准 RTC 信令房间尚未准备完成')
    if (this.pendingAnswer) throw new Error('已有 RTC SDP 协商正在进行')
    return new Promise<string>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.fail(new Error('标准 RTC Answer SDP 等待超时'))
      }, 15000)
      this.pendingAnswer = { resolve, reject, timer }
      this.canSendIce = true
      this.sendPxMessage({
        deviceId: this.clientId,
        streamId: this.params.streamId,
        type: MSG_TYPE_SIG_OFFER_SDP,
        sigOfferSdp: {
          deviceId: this.clientId,
          sdp,
          connectionTicket: ticket,
          clientNonce,
          instanceId,
          safetyPwdMd5: ticket ? '' : safetyPwdMd5,
        },
      })
      for (const candidate of this.pendingLocalIce.splice(0)) this.sendIce(candidate)
    })
  }

  sendIce(candidate: RTCIceCandidate) {
    if (!this.roomId || !this.canSendIce) {
      if (this.pendingLocalIce.length >= MAX_PENDING_LOCAL_ICE) {
        this.pendingLocalIce.shift()
        this.onLog('[rtc-standard] pending local ICE queue full; dropped oldest candidate')
      }
      this.pendingLocalIce.push(candidate)
      return
    }
    this.sendPxMessage({
      deviceId: this.clientId,
      streamId: this.params.streamId,
      type: MSG_TYPE_SIG_ICE,
      sigIce: {
        deviceId: this.clientId,
        ice: candidate.candidate,
        mid: candidate.sdpMid || '',
        sdpMlineIndex: candidate.sdpMLineIndex || 0,
      },
    })
  }

  stop() {
    this.teardown(new Error('标准 RTC 信令已停止'), false)
  }

  private sendRelay(value: Record<string, unknown>) {
    if (this.socket?.readyState !== WebSocket.OPEN) return
    this.socket.send(RelayMessage.encode(RelayMessage.create(value)).finish())
  }

  private sendPxMessage(value: Record<string, unknown>) {
    if (!this.roomId) return
    const payload = PxMessage.encode(PxMessage.create(value)).finish()
    this.sendRelay({
      type: typeValue('kRelayTargetMessage'),
      fromDeviceId: this.clientId,
      relay: { relayMsgIndex: this.relayIndex++, roomIds: [this.roomId], payload },
    })
  }

  private async onRelayMessage(raw: unknown) {
    if (this.closed) return
    const bytes = raw instanceof ArrayBuffer
      ? new Uint8Array(raw)
      : raw instanceof Blob
        ? new Uint8Array(await raw.arrayBuffer())
        : null
    if (!bytes) return
    const message = RelayMessage.decode(bytes) as unknown as Record<string, any>
    if (message.type === typeValue('kRelayCreateRoomResp')) {
      this.roomId = message.createRoomResp?.roomId || ''
      this.sendRelay({
        type: typeValue('kRelayRequestControl'),
        fromDeviceId: this.clientId,
        requestControl: {
          deviceId: this.clientId,
          remoteDeviceId: this.params.remoteDeviceId,
          roomId: this.roomId,
          deviceName: 'WebClient',
          streamId: this.params.streamId,
          forceGdi: false,
        },
      })
    } else if (message.type === typeValue('kRelayRoomPrepared')) {
      this.onLog(`[rtc-standard] signaling room ready: ${this.roomId}`)
      this.readyResolve?.()
      this.readyResolve = null
      this.readyReject = null
    } else if (message.type === typeValue('kRelayTargetMessage') && message.relay?.payload) {
      await this.onPxSignaling(new Uint8Array(message.relay.payload))
    } else if (message.type === typeValue('kRelayError')) {
      this.fail(new Error(message.relayError?.message || '标准 RTC Relay 拒绝会话'))
    }
  }

  private async onPxSignaling(bytes: Uint8Array) {
    const message = PxMessage.decode(bytes) as unknown as Record<string, any>
    if (message.type === MSG_TYPE_SIG_ANSWER_SDP) {
      const sdp = message.sigAnswerSdp?.sdp || ''
      if (sdp && this.pendingAnswer) {
        const pending = this.pendingAnswer
        this.pendingAnswer = null
        window.clearTimeout(pending.timer)
        pending.resolve(sdp)
      }
    } else if (message.type === MSG_TYPE_SIG_ICE && message.sigIce?.ice) {
      await this.onRemoteIce({
        candidate: message.sigIce.ice,
        sdpMid: message.sigIce.mid || null,
        sdpMLineIndex: message.sigIce.sdpMlineIndex || 0,
      })
    }
  }

  private fail(error: Error) {
    this.teardown(error, true)
  }

  private teardown(error: Error, report: boolean) {
    if (this.closed) return
    this.closed = true
    if (report) this.onLog(`[rtc-standard] ${error.message}`)
    const readyReject = this.readyReject
    this.readyResolve = null
    this.readyReject = null
    const pending = this.pendingAnswer
    this.pendingAnswer = null
    if (pending) window.clearTimeout(pending.timer)
    if (this.heartbeatTimer !== null) window.clearInterval(this.heartbeatTimer)
    this.heartbeatTimer = null
    const socket = this.socket
    this.socket = null
    this.roomId = ''
    this.pendingLocalIce.length = 0
    this.canSendIce = false
    socket?.close()
    readyReject?.(error)
    pending?.reject(error)
  }
}
