// tc.Message 运行时加载(protobufjs 动态解析,proto 源文件以 ?raw 内联进 bundle)
// px_message.proto import 了另外两个 proto;三者同 package tc,
// 先解析被依赖的文件、并剥掉 import 语句,即可在同一 Root 内完成解析。
import protobuf from 'protobufjs'
import tcFileTransferProto from '../../proto/px_file_transfer.proto?raw'
import tcSignalingProto from '../../proto/px_signaling_message.proto?raw'
import tcMessageProto from '../../proto/px_message.proto?raw'

const root = new protobuf.Root()
protobuf.parse(tcFileTransferProto, root)
protobuf.parse(tcSignalingProto, root)
protobuf.parse(tcMessageProto.replace(/^\s*import\s+"[^"]+"\s*;\s*$/gm, ''), root)

export const TcMessage = root.lookupType('tc.Message')

// MessageType 枚举值(px_message.proto)
export const MSG_TYPE_HELLO = 0 // kHello
export const MSG_TYPE_SERVER_CONFIGURATION = 2 // kServerConfiguration
export const MSG_TYPE_KEY_EVENT = 50 // kKeyEvent
export const MSG_TYPE_MOUSE_EVENT = 60 // kMouseEvent
export const MSG_TYPE_CLIPBOARD_INFO = 160 // kClipboardInfo
export const MSG_TYPE_MONITOR_SWITCHED = 180 // kMonitorSwitched
export const MSG_TYPE_CHANGE_MONITOR_RESOLUTION = 200 // kChangeMonitorResolution
export const MSG_TYPE_CHANGE_MONITOR_RESOLUTION_RESULT = 210 // kChangeMonitorResolutionResult
export const MSG_TYPE_SWITCH_FULL_COLOR_MODE = 460 // kSwitchFullColorMode
export const MSG_TYPE_CONNECTION_TAKEN_OVER = 550 // kConnectionTakenOver (render -> client)
export const MSG_TYPE_VIDEO_CODEC_CHANGED = 530 // kVideoCodecChanged (render -> client)
export const MSG_TYPE_GAME_STATUS_CHANGED = 540 // kGameStatusChanged (render -> client)
export const MSG_TYPE_INSTANCE_STOPPED = 560 // kInstanceStopped (render -> client)

// ClipboardType(px_message.proto:498-503)
export const CLIPBOARD_TYPE_TEXT = 0 // kClipboardText

// 文件传输相关 MessageType(px_message.proto:67-81)
export const MSG_TYPE_FILE_OPERATION_EVENT = 260 // kFileOperationEvent
export const MSG_TYPE_FILE_OPERATE_RESP_RENAME = 265 // kFileOperateRespRename
export const MSG_TYPE_FILE_OPERATE_RESP_GET_FILE_LIST = 270 // kFileOperateRespGetFileList
export const MSG_TYPE_FILE_OPERATE_RESP_CREATE_NEW_FOLDER = 280 // kFileOperateRespCreateNewFolder
export const MSG_TYPE_FILE_OPERATE_RESP_DEL = 290 // kFileOperateRespDel
export const MSG_TYPE_FILE_TRANS_RESP_UPLOAD = 300 // kFileTransRespUpload
export const MSG_TYPE_FILE_TRANS_RESP_DOWNLOAD = 305 // kFileTransRespDownload
export const MSG_TYPE_FILE_TRANS_DATA_PACKET = 311 // kFileTransDataPacket
export const MSG_TYPE_FILE_TRANS_DATA_PACKET_RESPONSE = 312 // kFileTransDataPacketResponse
export const MSG_TYPE_FILE_TRANS_SAVE_FILE_EXCEPTION = 320 // kFileTransSaveFileException

// FileOperateionsEvent.OperateType(px_file_transfer.proto:142-153)
export const FT_OP_DEL = 0
export const FT_OP_CREATE_NEW_FOLDER = 2
export const FT_OP_RENAME = 4
export const FT_OP_GET_FILES_LIST = 5
export const FT_OP_DOWNLOAD = 8
export const FT_OP_RECURSIVE_GET_FILES_LIST = 9

// FileTransDataPacket.TransmitDirection / TransmitState
export const FT_DIR_UPLOAD = 0
export const FT_DIR_DOWNLOAD = 1
export const FT_STATE_TRANSMITTING = 0
export const FT_STATE_END = 1
export const FT_STATE_ERROR = 2
export const FT_STATE_CANCEL = 3

// FileTransSaveFileException.SaveFileExceptionCause
export const FT_SAVE_EX_CANCEL = 2

// FileDescInfo.FileType
export const FT_FILE_TYPE_DISK = 0
export const FT_FILE_TYPE_FOLDER = 1
export const FT_FILE_TYPE_FILE = 2

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
  return TcMessage.encode(TcMessage.create(fields)).finish()
}

// tc.Message 解码;uint64 字段是 Long 对象,调用方按需 Number() 转换
export function decodeMessage(payload: Uint8Array) {
  return TcMessage.decode(payload) as unknown as {
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
    fileOperateRespSequence?: unknown
    fileOperateRespCode?: number
    fileOperateRespMessage?: string
    fileOperateRespGetFileList?: {
      ret: boolean
      msgOfError: string
      path: string
      fileInfos: Array<{ type: number; name: string; path: string; size: unknown; date: unknown }>
    }
    fileOperateRespRename?: {
      ret: boolean
      pathOfOld: string
      pathOfNew: string
      msgOfError: string
    }
    fileOperateRespCreateNewFolder?: {
      ret: boolean
      pathOfParent: string
      pathOfNewCreated: string
      msgOfError: string
    }
    fileOperateRespDel?: {
      ret: boolean
      pathsOfNoDel: string[]
      msgOfError: string
    }
    fileTransRespUpload?: {
      res: boolean
      errorCause: number
      srcFilePath: string
      targetFilePath: string
      taskId: string
    }
    fileTransRespDownload?: {
      res: boolean
      errorCause: number
      srcFilePath: string
      targetFilePath: string
      taskId: string
    }
    fileTransDataPacket?: {
      transmitDirection: number
      srcFilePath: string
      targetFilePath: string
      fileSize: unknown
      taskId: string
      index: unknown
      transmitState: number
      data: Uint8Array
    }
    fileTransDataPacketResponse?: { taskId: string; index: unknown }
    clipboardInfo?: { type: number; msg: Uint8Array }
  }
}
