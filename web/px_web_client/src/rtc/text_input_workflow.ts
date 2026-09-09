/** Local editing only. The owner supplies an authenticated, reliable transport.
 * No input or clipboard side effects occur in this model.
 */
export type TextTarget = Readonly<{
  instanceId: string
  leaseGeneration: string
  targetGeneration: string
  inputGeneration: string
  maxBytes: number
}>

export type TextSubmission = Readonly<{
  requestId: string
  target: TextTarget
  text: string
}>

export type TextOutcome = 'accepted' | 'submitted' | 'permission_denied' | 'target_changed'
  | 'target_unavailable' | 'invalid_text' | 'busy' | 'failed' | 'outcome_unknown' | 'unsupported'

export type TextInputModel = Pick<TextInputWorkflow, keyof TextInputWorkflow>

export function validSubmissionText(text: string, maxBytes: number): boolean {
  if (!text || !Number.isInteger(maxBytes) || maxBytes < 1 || maxBytes > 16384) return false
  // Reject unpaired UTF-16 surrogates before TextEncoder silently replaces them.
  for (let index = 0; index < text.length; index++) {
    const code = text.charCodeAt(index)
    if ((code < 32 && code !== 9 && code !== 10 && code !== 13) || code === 127) return false
    if (code >= 0xd800 && code <= 0xdbff) {
      const next = text.charCodeAt(++index)
      if (!(next >= 0xdc00 && next <= 0xdfff)) return false
    } else if (code >= 0xdc00 && code <= 0xdfff) return false
  }
  return new TextEncoder().encode(text).byteLength <= maxBytes
}

export class TextInputWorkflow {
  private target: TextTarget | null = null
  private instanceId = ''
  private draftValue = ''
  private revision = 0
  private pending: { submission: TextSubmission; revision: number } | null = null
  private usedIds = new Set<string>()
  private panelOpen = false
  private composingValue = false
  private outcomeValue: TextOutcome | null = null

  get draft() { return this.draftValue }
  get isOpen() { return this.panelOpen }
  get isComposing() { return this.composingValue }
  get outcome() { return this.outcomeValue }
  get suppressOrdinaryInput() { return this.panelOpen || this.pending !== null }
  get canSend() {
    return this.panelOpen && !this.composingValue && !this.pending && !!this.target
      && validSubmissionText(this.draftValue, this.target.maxBytes)
  }

  /** Called only after capability/lease validation and an acknowledged editing barrier. */
  bind(target: TextTarget): void {
    if (this.instanceId !== target.instanceId) this.clear()
    else this.disconnect()
    this.instanceId = target.instanceId
    this.target = Object.freeze({ ...target })
    // Reconnection must not silently re-enable a still-open editor.
    this.panelOpen = false
  }

  /** Select the in-memory draft scope before a user can type while the barrier is pending. */
  prepareInstance(instanceId: string): void {
    if (this.instanceId !== instanceId) this.clear()
    this.instanceId = instanceId
  }

  open(): boolean {
    if (!this.target) return false
    this.panelOpen = true
    return true
  }

  close(): void {
    this.panelOpen = false
    this.composingValue = false
  }

  edit(text: string): void {
    this.draftValue = text
    this.revision++
  }

  setComposing(value: boolean): void { this.composingValue = value }

  begin(requestId: string): TextSubmission | null {
    if (!this.canSend || !this.target || !/^[A-Za-z0-9_-]{1,64}$/.test(requestId)
      || this.usedIds.has(requestId) || this.usedIds.size >= 4096) return null
    const submission = Object.freeze({ requestId, target: this.target, text: this.draftValue })
    this.usedIds.add(requestId)
    this.pending = { submission, revision: this.revision }
    this.outcomeValue = null
    return submission
  }

  /** The adapter must associate a reply with its connection's lease generation. */
  resolve(requestId: string, leaseGeneration: string, outcome: TextOutcome): boolean {
    const pending = this.pending
    if (!pending || pending.submission.requestId !== requestId
      || pending.submission.target.leaseGeneration !== leaseGeneration) return false
    this.outcomeValue = outcome
    if (outcome === 'accepted') return true
    if (outcome === 'submitted' && this.revision === pending.revision) this.edit('')
    this.pending = null
    return true
  }

  disconnect(): void {
    if (this.pending) this.outcomeValue = 'outcome_unknown'
    this.pending = null
    this.target = null
    this.close()
  }

  /** Permission loss, logout, instance switch, or owner destruction. */
  clear(): void {
    this.disconnect()
    this.instanceId = ''
    this.edit('')
    this.usedIds.clear()
    this.outcomeValue = null
  }
}
