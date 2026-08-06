// 剪贴板文本同步:经 media_data_channel 发送/接收 NetTlvHeader + tc.Message
// 协议(双向均为 kClipboardInfo=160,对齐 C++ 端):
//   web -> render: BaseWorkspace::SendClipboardMessage(ct_base_workspace.cpp:880)
//     render plugin_net_event_router.cpp:136 把原文转发给 gr_user_proxy,由其写入系统剪贴板
//   render -> web: gr_user_proxy 监听系统剪贴板 -> kRpClipboardEvent ->
//     ws_user_proxy_router.cpp:92 转为 kClipboardInfo 广播到所有流
// 注意:ClipboardInfo.msg 是 bytes(protobufjs 对 string 会按 base64 处理),
//   必须 TextEncoder/TextDecoder 显式转换
import { packTlv } from './tlv'
import { encodeMessage, decodeMessage, MSG_TYPE_CLIPBOARD_INFO, CLIPBOARD_TYPE_TEXT } from './proto'

let pktIndex = 0n

/** Browser can read local clipboard (secure context + Clipboard API). */
export function canReadLocalClipboard(): boolean {
  return (
    typeof window !== 'undefined' &&
    window.isSecureContext === true &&
    !!navigator.clipboard &&
    typeof navigator.clipboard.readText === 'function'
  )
}

export function sendClipboardText(
  dc: RTCDataChannel | null,
  deviceId: string,
  streamId: string,
  text: string,
): boolean {
  if (!dc || dc.readyState !== 'open' || !text) return false
  const payload = encodeMessage({
    type: MSG_TYPE_CLIPBOARD_INFO,
    deviceId,
    streamId,
    clipboardInfo: {
      type: CLIPBOARD_TYPE_TEXT,
      msg: new TextEncoder().encode(text),
    },
  })
  dc.send(packTlv(payload, pktIndex++))
  return true
}

// 从重组后的 tc.Message payload 中提取剪贴板文本;非剪贴板消息返回 null
export function parseClipboardText(payload: Uint8Array): string | null {
  let msg: ReturnType<typeof decodeMessage>
  try {
    msg = decodeMessage(payload)
  } catch {
    return null
  }
  if (msg.type !== MSG_TYPE_CLIPBOARD_INFO) return null
  const info = msg.clipboardInfo
  if (!info || info.type !== CLIPBOARD_TYPE_TEXT || !info.msg || info.msg.length === 0) return null
  return new TextDecoder().decode(info.msg)
}
