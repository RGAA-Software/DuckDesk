export type VoiceCallPhase = 'idle' | 'outgoing' | 'connected' | 'error'

export interface VoiceCallIdentity {
  callId: string
  requestId: string
}

export interface VoiceCallResponseLike {
  callId: string
  requestId: number | string | { toString(): string }
}

export interface VoiceAudioConfigLike {
  callId: string
  sampleRate: number
  channels: number
  frameMs: number
}

export function matchesPendingVoiceResponse(
  phase: VoiceCallPhase,
  identity: VoiceCallIdentity,
  response: VoiceCallResponseLike,
): boolean {
  return phase === 'outgoing'
    && identity.callId.length > 0
    && identity.requestId.length > 0
    && response.callId === identity.callId
    && String(response.requestId) === identity.requestId
}

export function matchesActiveVoiceCall(
  identity: VoiceCallIdentity,
  callId: string,
): boolean {
  return identity.callId.length > 0 && callId === identity.callId
}

export function isSupportedVoiceAudioConfig(
  identity: VoiceCallIdentity,
  config: VoiceAudioConfigLike,
): boolean {
  return matchesActiveVoiceCall(identity, config.callId)
    && config.sampleRate === 48_000
    && config.channels === 1
    && config.frameMs === 20
}
