// Wire values mirror px_message.proto; protocol_enums.test.ts checks every member.
// Keep these names in business code; raw numbers belong only to serialization boundaries.
export enum MessageType {
  Hello = 0,
  ServerConfiguration = 2,
  HeartBeat = 20,
  KeyEvent = 50,
  MouseEvent = 60,
  TextInput = 580,
  ApplicationTextCapabilities = 610,
  ApplicationTextState = 611,
  ApplicationTextSubmit = 612,
  ApplicationTextResult = 613,
  ApplicationTextBarrier = 614,
  ApplicationTextBarrierResult = 615,
  ClipboardInfo = 160,
  ClipboardInfoResp = 161,
  MonitorSwitched = 180,
  ChangeMonitorResolution = 200,
  ChangeMonitorResolutionResult = 210,
  FileAction = 270,
  FileResponse = 280,
  SwitchFullColorMode = 460,
  ConnectionTakenOver = 550,
  VideoCodecChanged = 530,
  GameStatusChanged = 540,
  InstanceStopped = 560,
  VirtualDisplayRequest = 570,
  VirtualDisplayResponse = 571,
  VoiceCallRequest = 590,
  VoiceCallResponse = 591,
  VoiceAudioConfig = 592,
  SigOfferSdp = 370,
  SigAnswerSdp = 380,
  SigIce = 390,
  VideoFrame = 30,
  AudioFrame = 40,
  SwitchMonitor = 170,
}

export enum TextEditability { Unknown = 0, Editable = 1, NotEditable = 2 }
export enum TextOutcomeCode {
  Unspecified = 0, Accepted = 1, Submitted = 2, Unsupported = 3, PermissionDenied = 4,
  TargetChanged = 5, TargetUnavailable = 6, Invalid = 7, Busy = 8, Failed = 9, Unknown = 10,
}
export enum VideoCodec { H264 = 0, Hevc = 1, Vp9 = 2 }
export enum GameStatus { Running = 0, Died = 1, Restarting = 2 }
export enum VirtualDisplayState { Ready = 0, NeedReconnect = 1, Failed = 2 }
export enum VirtualDisplayOperation { Create = 0, RemoveLast = 1, Query = 2, ResetOwned = 3 }
export enum ClientType { Windows = 0, Linux = 1, MacOS = 2, Android = 3, IOS = 4, Unknown = 100 }
