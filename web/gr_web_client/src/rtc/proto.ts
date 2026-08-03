// tc.Message 运行时加载(protobufjs 动态解析,proto 源文件以 ?raw 内联进 bundle)
// tc_message.proto import 了另外两个 proto;三者同 package tc,
// 先解析被依赖的文件、并剥掉 import 语句,即可在同一 Root 内完成解析。
import protobuf from 'protobufjs'
import tcFileTransferProto from '../../proto/tc_file_transfer.proto?raw'
import tcSignalingProto from '../../proto/tc_signaling_message.proto?raw'
import tcMessageProto from '../../proto/tc_message.proto?raw'

const root = new protobuf.Root()
protobuf.parse(tcFileTransferProto, root)
protobuf.parse(tcSignalingProto, root)
protobuf.parse(tcMessageProto.replace(/^\s*import\s+"[^"]+"\s*;\s*$/gm, ''), root)

export const TcMessage = root.lookupType('tc.Message')

// MessageType 枚举值(tc_message.proto)
export const MSG_TYPE_KEY_EVENT = 50 // kKeyEvent
export const MSG_TYPE_MOUSE_EVENT = 60 // kMouseEvent
export const MSG_TYPE_CLIPBOARD_INFO = 160 // kClipboardInfo

// ClipboardType(tc_message.proto:498-503)
export const CLIPBOARD_TYPE_TEXT = 0 // kClipboardText

// 文件传输相关 MessageType(tc_message.proto:67-81)
export const MSG_TYPE_FILE_OPERATION_EVENT = 260 // kFileOperationEvent
export const MSG_TYPE_FILE_OPERATE_RESP_GET_FILE_LIST = 270 // kFileOperateRespGetFileList
export const MSG_TYPE_FILE_TRANS_RESP_UPLOAD = 300 // kFileTransRespUpload
export const MSG_TYPE_FILE_TRANS_RESP_DOWNLOAD = 305 // kFileTransRespDownload
export const MSG_TYPE_FILE_TRANS_DATA_PACKET = 311 // kFileTransDataPacket
export const MSG_TYPE_FILE_TRANS_DATA_PACKET_RESPONSE = 312 // kFileTransDataPacketResponse
export const MSG_TYPE_FILE_TRANS_SAVE_FILE_EXCEPTION = 320 // kFileTransSaveFileException

// FileOperateionsEvent.OperateType(tc_file_transfer.proto:142-153)
export const FT_OP_GET_FILES_LIST = 5
export const FT_OP_DOWNLOAD = 8

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

// ButtonFlag 位掩码(tc_message.proto:143-158)
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
    fileOperateRespSequence?: unknown
    fileOperateRespCode?: number
    fileOperateRespMessage?: string
    fileOperateRespGetFileList?: {
      ret: boolean
      msgOfError: string
      path: string
      fileInfos: Array<{ type: number; name: string; path: string; size: unknown; date: unknown }>
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
