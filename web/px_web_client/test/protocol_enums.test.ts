import { describe, expect, it } from 'vitest'
import { PxMessage } from '../src/rtc/proto'
import {
  MessageType, TextEditability, TextOutcomeCode, VideoCodec, GameStatus,
  VirtualDisplayState, VirtualDisplayOperation, ClientType,
} from '../src/rtc/protocol_enums'

function checkWireEnum(name: string, members: Readonly<Record<string, number>>) {
  const values = PxMessage.root.lookupEnum(name).values
  for (const [member, value] of Object.entries(members)) expect(value, name + '.' + member).toBe(values[member])
}

describe('semantic protocol enums', () => {
  it('preserves every message wire identifier', () => {
    checkWireEnum('px.MessageType', {
      kHello: MessageType.Hello,
      kServerConfiguration: MessageType.ServerConfiguration,
      kHeartBeat: MessageType.HeartBeat,
      kKeyEvent: MessageType.KeyEvent,
      kMouseEvent: MessageType.MouseEvent,
      kTextInput: MessageType.TextInput,
      kApplicationTextCapabilities: MessageType.ApplicationTextCapabilities,
      kApplicationTextState: MessageType.ApplicationTextState,
      kApplicationTextSubmit: MessageType.ApplicationTextSubmit,
      kApplicationTextResult: MessageType.ApplicationTextResult,
      kApplicationTextBarrier: MessageType.ApplicationTextBarrier,
      kApplicationTextBarrierResult: MessageType.ApplicationTextBarrierResult,
      kClipboardInfo: MessageType.ClipboardInfo,
      kClipboardInfoResp: MessageType.ClipboardInfoResp,
      kMonitorSwitched: MessageType.MonitorSwitched,
      kChangeMonitorResolution: MessageType.ChangeMonitorResolution,
      kChangeMonitorResolutionResult: MessageType.ChangeMonitorResolutionResult,
      kFileAction: MessageType.FileAction,
      kFileResponse: MessageType.FileResponse,
      kSwitchFullColorMode: MessageType.SwitchFullColorMode,
      kConnectionTakenOver: MessageType.ConnectionTakenOver,
      kVideoCodecChanged: MessageType.VideoCodecChanged,
      kGameStatusChanged: MessageType.GameStatusChanged,
      kInstanceStopped: MessageType.InstanceStopped,
      kVirtualDisplayRequest: MessageType.VirtualDisplayRequest,
      kVirtualDisplayResponse: MessageType.VirtualDisplayResponse,
      kVoiceCallRequest: MessageType.VoiceCallRequest,
      kVoiceCallResponse: MessageType.VoiceCallResponse,
      kVoiceAudioConfig: MessageType.VoiceAudioConfig,
      kSigOfferSdpMessage: MessageType.SigOfferSdp,
      kSigAnswerSdpMessage: MessageType.SigAnswerSdp,
      kSigIceMessage: MessageType.SigIce,
      kVideoFrame: MessageType.VideoFrame,
      kAudioFrame: MessageType.AudioFrame,
      kSwitchMonitor: MessageType.SwitchMonitor,
    })
  })
  it('preserves state, outcome, codec, client and operation wire identifiers', () => {
    checkWireEnum('px.ApplicationTextState.Editability', {
      UNKNOWN: TextEditability.Unknown, EDITABLE: TextEditability.Editable, NOT_EDITABLE: TextEditability.NotEditable,
    })
    checkWireEnum('px.ApplicationTextOutcome', {
      TEXT_OUTCOME_UNSPECIFIED: TextOutcomeCode.Unspecified, TEXT_ACCEPTED: TextOutcomeCode.Accepted,
      TEXT_SUBMITTED: TextOutcomeCode.Submitted, TEXT_UNSUPPORTED: TextOutcomeCode.Unsupported,
      TEXT_PERMISSION_DENIED: TextOutcomeCode.PermissionDenied, TEXT_TARGET_CHANGED: TextOutcomeCode.TargetChanged,
      TEXT_TARGET_UNAVAILABLE: TextOutcomeCode.TargetUnavailable, TEXT_INVALID: TextOutcomeCode.Invalid,
      TEXT_BUSY: TextOutcomeCode.Busy, TEXT_FAILED: TextOutcomeCode.Failed, TEXT_OUTCOME_UNKNOWN: TextOutcomeCode.Unknown,
    })
    checkWireEnum('px.VideoType', { kNetH264: VideoCodec.H264, kNetHevc: VideoCodec.Hevc, kNetVp9: VideoCodec.Vp9 })
    checkWireEnum('px.GameStatusChanged.GameStatus', {
      kGameRunning: GameStatus.Running, kGameDied: GameStatus.Died, kGameRestarting: GameStatus.Restarting,
    })
    checkWireEnum('px.VirtualDisplayResponseState', {
      kVirtualDisplayReady: VirtualDisplayState.Ready, kVirtualDisplayNeedReconnect: VirtualDisplayState.NeedReconnect,
      kVirtualDisplayFailed: VirtualDisplayState.Failed,
    })
    checkWireEnum('px.RemoteVirtualDisplayOperation', {
      kRemoteVirtualDisplayCreate: VirtualDisplayOperation.Create, kRemoteVirtualDisplayRemoveLast: VirtualDisplayOperation.RemoveLast,
      kRemoteVirtualDisplayQuery: VirtualDisplayOperation.Query, kRemoteVirtualDisplayResetOwned: VirtualDisplayOperation.ResetOwned,
    })
    checkWireEnum('px.ClientType', {
      kWindows: ClientType.Windows, kLinux: ClientType.Linux, kMacOS: ClientType.MacOS,
      kAndroid: ClientType.Android, kiOS: ClientType.IOS, kUnknown: ClientType.Unknown,
    })
  })
})
