import type { TextInputModel, TextOutcome, TextSubmission, TextTarget } from './text_input_workflow'

export type ApplicationTextTarget = Omit<TextTarget, 'inputGeneration' | 'maxBytes'>
export interface ApplicationTextMessage {
  type: number
  applicationTextCapabilities?: {
    version: number; maxUtf8Bytes: number; finalTextSupported: boolean; stateHintsSupported: boolean
    parentBindingId: string; inputGeneration: string
  }
  applicationTextState?: { target?: ApplicationTextTarget; editability: number }
  applicationTextBarrierResult?: {
    requestId: string; target?: ApplicationTextTarget; outcome: number; inputGeneration: string; editing: boolean
  }
  applicationTextResult?: { requestId: string; target?: ApplicationTextTarget; outcome: number }
}

const outcomes: Readonly<Record<number, TextOutcome>> = {
  1: 'accepted', 2: 'submitted', 3: 'unsupported', 4: 'permission_denied', 5: 'target_changed',
  6: 'target_unavailable', 7: 'invalid_text', 8: 'busy', 9: 'failed', 10: 'outcome_unknown',
}

export function textRequestId(): string {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('')
}

export function reliableApplicationControlChannel(channel: Pick<RTCDataChannel,
  'ordered' | 'maxRetransmits' | 'maxPacketLifeTime' | 'readyState'> | null): boolean {
  return !!channel && channel.readyState === 'open' && channel.ordered
    && channel.maxRetransmits === null && channel.maxPacketLifeTime === null
}

interface TextTransportOptions {
  workflow: TextInputModel
  instanceId: string
  suspend: (value: boolean, generation: string) => void
  changed: (ready: boolean, hint: boolean, status: string) => void
  send: (fields: Record<string, unknown>) => boolean
  canSend: () => boolean
}

/** Reuses the authenticated reliable control carrier; it never creates or closes a connection. */
export class ApplicationTextTransport {
  private started = false
  private target: ApplicationTextTarget | null = null
  private maxBytes = 0
  private generation = '0'
  private ready = false
  private editing = false
  private pending: { id: string; begin: boolean } | null = null
  private timer: ReturnType<typeof setTimeout> | null = null
  private hintTimer: ReturnType<typeof setInterval> | null = null
  private alive = true
  private editability = 0
  private closeAfterBegin = false
  private awaitingSubmission = false
  private closeAfterSubmission = false
  private invalidTarget = false

  constructor(private readonly opts: TextTransportOptions) { opts.workflow.prepareInstance(opts.instanceId) }

  start() {
    if (!this.alive || this.started || !this.opts.canSend()) return
    this.started = true
    this.send({ type: 610, applicationTextCapabilities: { version: 1 } })
    // Advisory queries share the existing reliable control channel; no new
    // connection, ticket exchange, media path or unbounded queue is created.
    this.hintTimer = setInterval(() => {
      if (!this.pending && !this.awaitingSubmission && this.opts.canSend()) {
        this.send({ type: 610, applicationTextCapabilities: { version: 1 } })
      }
    }, 750)
    this.armTimeout('远端文字输入能力确认超时，请重新连接。')
  }

  private send(fields: Record<string, unknown>): boolean {
    if (!this.alive || !this.started || !this.opts.canSend()) return false
    try {
      return this.opts.send(fields)
    } catch { this.fail('文字通道发送失败；请先检查远端结果。'); return false }
  }

  private clearTimer() { if (this.timer !== null) clearTimeout(this.timer); this.timer = null }
  private armTimeout(message: string) {
    this.clearTimer()
    this.timer = setTimeout(() => this.fail(message), 10000)
  }

  private validTarget(target?: ApplicationTextTarget): target is ApplicationTextTarget {
    return !!target && target.instanceId === this.opts.instanceId
      && /^\d+$/.test(target.leaseGeneration) && /^[A-Za-z0-9:_-]{1,128}$/.test(target.targetGeneration)
  }

  receive(message: ApplicationTextMessage) {
    if (!this.alive || !this.started) return
    const caps = message.applicationTextCapabilities
    if (message.type === 610 && caps) {
      if (caps.version !== 1 || !caps.finalTextSupported || caps.maxUtf8Bytes < 1 || caps.maxUtf8Bytes > 16384) {
        this.fail('远端不支持文字输入。'); return
      }
      this.maxBytes = caps.maxUtf8Bytes
      this.generation = caps.inputGeneration || '0'
    }
    const state = message.applicationTextState
    if (message.type === 611 && state && this.validTarget(state.target)) {
      const changed = this.target && (state.target.targetGeneration !== this.target.targetGeneration
        || state.target.leaseGeneration !== this.target.leaseGeneration)
      this.target = { ...state.target }
      this.editability = state.editability
      if (changed && (this.editing || this.pending)) {
        this.invalidTarget = true
        this.opts.workflow.disconnect()
        this.opts.changed(false, false, '远端输入目标已改变，请关闭面板并重新选择输入位置。')
        return
      }
    }
    const barrier = message.applicationTextBarrierResult
    if (message.type === 615 && barrier && this.pending?.id === barrier.requestId) {
      const begin = this.pending.begin
      this.pending = null
      this.clearTimer()
      if (barrier.outcome !== 2 || barrier.editing !== begin || !this.validTarget(barrier.target)
        || !/^\d+$/.test(barrier.inputGeneration)) {
        this.fail('输入屏障未确认，请重新连接后再操作。'); return
      }
      this.generation = barrier.inputGeneration
      this.target = { ...barrier.target }
      this.editing = begin
      this.invalidTarget = false
      this.opts.suspend(begin, this.generation)
      if (begin) {
        const composing = this.opts.workflow.isComposing
        this.opts.workflow.bind({ ...barrier.target, inputGeneration: this.generation, maxBytes: this.maxBytes })
        this.opts.workflow.open()
        this.opts.workflow.setComposing(composing)
      } else this.opts.workflow.close()
      this.opts.changed(this.ready, this.editability === 1, begin ? '可以使用本机输入法编辑。' : '')
      if (begin && this.closeAfterBegin) { this.closeAfterBegin = false; this.endEditing() }
      return
    }
    const result = message.applicationTextResult
    if (message.type === 613 && result && this.validTarget(result.target)) {
      const outcome = outcomes[result.outcome] ?? 'outcome_unknown'
      if (this.opts.workflow.resolve(result.requestId, result.target.leaseGeneration, outcome) && outcome !== 'accepted') {
        this.clearTimer()
        this.awaitingSubmission = false
        if (this.closeAfterSubmission) { this.closeAfterSubmission = false; this.endEditing() }
      }
      return
    }
    if (this.target && this.maxBytes > 0 && !this.pending) {
      if (this.invalidTarget && this.editing) {
        this.opts.changed(false, false, '远端输入目标已改变，请关闭面板并重新选择输入位置。')
        return
      }
      if (!this.ready) this.clearTimer()
      this.ready = true
      this.opts.changed(true, this.editability === 1, '')
      this.opts.suspend(this.editing, this.generation)
    }
  }

  beginEditing(): boolean { return this.barrier(true) }
  endEditing(): boolean {
    this.opts.workflow.close()
    if (this.pending?.begin) { this.closeAfterBegin = true; return true }
    if (this.awaitingSubmission) { this.closeAfterSubmission = true; return true }
    return this.barrier(false)
  }

  private barrier(begin: boolean): boolean {
    if (!this.ready || !this.target || this.pending || (begin && this.editing)) return false
    this.opts.suspend(true, this.generation)
    const id = textRequestId()
    this.pending = { id, begin }
    if (!this.send({ type: 614, applicationTextBarrier: {
      requestId: id, target: this.target, beginEditing: begin, expectedInputGeneration: this.generation,
    } })) { this.fail('文字通道不可用，请重新连接。'); return false }
    this.armTimeout('输入屏障确认超时；请重新连接，避免旧按键重新生效。')
    return true
  }

  submit(submission: TextSubmission) {
    if (!this.editing || this.pending || !this.send({ type: 612, applicationTextSubmit: {
      requestId: submission.requestId, target: submission.target, text: submission.text,
      inputGeneration: submission.target.inputGeneration,
    } })) { this.fail('提交结果不确定，请先查看远端，不要重复发送。'); return }
    this.awaitingSubmission = true
    this.armTimeout('提交回执超时，请先查看远端，不要重复发送。')
  }

  private fail(status: string) {
    if (!this.alive) return
    this.clearTimer()
    this.ready = false
    this.opts.workflow.disconnect()
    // After a lost barrier reply the ordinary input generation is unknown. Keep
    // input suspended until a new primary connection establishes a fresh lease.
    if (this.pending || this.editing) this.opts.suspend(true, this.generation)
    this.opts.changed(false, false, status)
    this.stopPolling()
  }

  private stopPolling() {
    if (this.hintTimer !== null) clearInterval(this.hintTimer)
    this.hintTimer = null
    this.started = false
  }

  dispose() {
    if (!this.alive) return
    this.alive = false
    this.clearTimer()
    this.stopPolling()
    this.opts.workflow.disconnect()
  }

  disconnected() { this.fail('控制通道已断开；草稿保留，不会自动重发。') }
}
