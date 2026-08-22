// px.Message 运行时加载(protobufjs 动态解析,proto 源文件以 ?raw 内联进 bundle)
// px_message.proto import 了 px_signaling_message.proto;两者同 package px,
// 先解析被依赖的文件、并剥掉 import 语句,即可在同一 Root 内完成解析。
import protobuf from 'protobufjs'
import pxSignalingProto from '../../proto/px_signaling_message.proto?raw'
import pxFileTransferProto from '../../proto/px_file_transfer.proto?raw'
import pxMessageProto from '../../proto/px_message.proto?raw'

const root = new protobuf.Root()
protobuf.parse(pxSignalingProto, root)
protobuf.parse(pxFileTransferProto, root)
protobuf.parse(pxMessageProto.replace(/^\s*import\s+"[^"]+"\s*;\s*$/gm, ''), root)

export const PxMessage = root.lookupType('px.Message')

// MessageType 枚举值(px_message.proto)
export const MSG_TYPE_HELLO = 0 // kHello
export const MSG_TYPE_SERVER_CONFIGURATION = 2 // kServerConfiguration
export const MSG_TYPE_KEY_EVENT = 50 // kKeyEvent
export const MSG_TYPE_MOUSE_EVENT = 60 // kMouseEvent
export const MSG_TYPE_CLIPBOARD_INFO = 160 // kClipboardInfo
export const MSG_TYPE_MONITOR_SWITCHED = 180 // kMonitorSwitched
export const MSG_TYPE_CHANGE_MONITOR_RESOLUTION = 200 // kChangeMonitorResolution
export const MSG_TYPE_CHANGE_MONITOR_RESOLUTION_RESULT = 210 // kChangeMonitorResolutionResult
export const MSG_TYPE_FILE_ACTION = 270 // kFileAction (client -> render,rustdesk 语义)
export const MSG_TYPE_FILE_RESPONSE = 280 // kFileResponse (render -> client,rustdesk 语义)
export const MSG_TYPE_SWITCH_FULL_COLOR_MODE = 460 // kSwitchFullColorMode
export const MSG_TYPE_CONNECTION_TAKEN_OVER = 550 // kConnectionTakenOver (render -> client)
export const MSG_TYPE_VIDEO_CODEC_CHANGED = 530 // kVideoCodecChanged (render -> client)
export const MSG_TYPE_GAME_STATUS_CHANGED = 540 // kGameStatusChanged (render -> client)
export const MSG_TYPE_INSTANCE_STOPPED = 560 // kInstanceStopped (render -> client)
export const MSG_TYPE_VIRTUAL_DISPLAY_REQUEST = 570 // kVirtualDisplayRequest (client -> render)
export const MSG_TYPE_VIRTUAL_DISPLAY_RESPONSE = 571 // kVirtualDisplayResponse (render -> client)

// ClipboardType(px_message.proto)
export const CLIPBOARD_TYPE_TEXT = 0 // kClipboardText

// KeyEvent.LockKeyStatusCheck
export const LOCK_KEY_DONT_CARE = 0
export const LOCK_KEY_CHECK_NUM_LOCK = 1
export const LOCK_KEY_CHECK_CAPS_LOCK = 2

// ButtonFlag 位掩码(px_message.proto:143-158)
export const BTN_LEFT_UP = 16
export const BTN_MIDDLE_UP = 32
export const BTN_RIGHT_UP = 64
export const BTN_MOUSE_MOVE = 128
export const BTN_WHEEL = 256
export const BTN_HWHEEL = 512
export const BTN_LEFT_DOWN = 1024
export const BTN_MIDDLE_DOWN = 2048
export const BTN_RIGHT_DOWN = 4096

export function encodeMessage(fields: Record<string, unknown>): Uint8Array {
  return PxMessage.encode(PxMessage.create(fields)).finish()
}

// px.Message 解码;uint64 字段是 Long 对象,调用方按需 Number() 转换
export function decodeMessage(payload: Uint8Array) {
  return PxMessage.decode(payload) as unknown as {
    type: number
    deviceId?: string
    streamId?: string
    // kServerConfiguration(type=2):render 在收到 kHello 后推送(含可用分辨率列表)
    config?: {
      monitorsInfo?: Array<{
        name: string
        resolutions?: Array<{ width: number; height: number }>
        primary: boolean
        currentWidth: number
        currentHeight: number
      }>
      capturingMonitorName: string
      fps: number
      fileTransferEnabled: boolean
      // 文件传输协议版本(rustdesk 语义 = 2;0/缺省为旧版,不兼容)
      ftProtocolVersion?: number
      virtualDisplayEnabled?: boolean
      virtualDisplayOwnedCount?: number
      virtualDisplayMaxCount?: number
      topologyGeneration?: number | { toString(): string }
    }
    // kMonitorSwitched(type=180):采集显示器已切换(切屏回包,含最新显示器列表)
    monitorSwitched?: { name: string; index: number }
    // kChangeMonitorResolutionResult(type=210):分辨率切换结果
    changeMonitorResolutionResult?: { monitorName: string; result: boolean }
    // kVideoCodecChanged(type=530):编码格式切换(H264/H265)
    videoCodecChanged?: { videoType: number; fullColor: boolean; reason: string }
    // kGameStatusChanged(type=540):game-hook 游戏状态(0=运行/恢复, 1=死亡, 2=重启中)
    gameStatusChanged?: { status: number; detail: string }
    // kInstanceStopped(type=560):实例被 CMS 停止,客户端应提示并断开
    instanceStopped?: { reason: string }
    // kVirtualDisplayResponse(type=571):虚拟显示器拓扑操作结果
    virtualDisplayResponse?: {
      requestId: string
      accepted: boolean
      state: number
      topologyChanged: boolean
      topologyGeneration: number | { toString(): string }
      logicalDisplayId: string
      errorCode: string
      errorMessage: string
      ownedDisplayCount: number
      actualUsbmmiddCount: number
      driverInstalled: boolean
      packageValid: boolean
      removalSafe: boolean
    }
    clipboardInfo?: { type: number; msg: Uint8Array }
  }
}
