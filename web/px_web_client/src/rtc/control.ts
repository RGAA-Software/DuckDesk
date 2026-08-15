// render 控制消息发送:经 media_data_channel 发送 NetTlvHeader + tc.Message
// 协议对齐 src/px_client/ct_base_workspace.cpp 与
// src/px_deps/px_message_new/proto_message_maker.cpp(MakeLockDevice/MakeStopRender/MakeCtrlAltDelete)
import { packTlv } from './tlv'
import { encodeMessage } from './proto'

// MessageType 枚举值(px_message.proto)
export const MSG_TYPE_SWITCH_MONITOR = 170 // kSwitchMonitor
export const MSG_TYPE_LOCK_DEVICE = 328 // kLockDevice
export const MSG_TYPE_STOP_RENDER = 329 // kStopRender
export const MSG_TYPE_REQ_CTRL_ALT_DELETE = 330 // kReqCtrlAltDelete
export const MSG_TYPE_HARD_UPDATE_DESKTOP = 341 // kHardUpdateDesktop
export const MSG_TYPE_MODIFY_FPS = 480 // kModifyFps

let pktIndex = 0n

// fields 为 tc.Message 的 camelCase 字段(protobufjs 默认转换),
// deviceId/streamId 与 C++ 端 Make* 一致地带上
export function sendControlMessage(
  dc: RTCDataChannel | null,
  deviceId: string,
  streamId: string,
  fields: Record<string, unknown>,
): boolean {
  if (!dc || dc.readyState !== 'open') return false
  const payload = encodeMessage({
    deviceId,
    streamId,
    ...fields,
  })
  dc.send(packTlv(payload, pktIndex++))
  return true
}
