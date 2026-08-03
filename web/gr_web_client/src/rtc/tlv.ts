// NetTlvHeader 打包/解包,对齐 src/gr_deps/tc_common_new/net_tlv_header.h
// 布局(小端):
//   u32 type_                  0   整包 = 1 (kNetTlvFull)
//   u32 this_buffer_length_    4   payload 长度(不含头部,见 rtc_data_channel.cpp 收发两侧)
//   u32 this_buffer_begin_     8   整包 = 0
//   u32 this_buffer_end_       12  = this_buffer_length_
//   u64 pkt_index_             16  单调递增(每条连接独立计数)
//   u32 parent_buffer_length_  24  = this_buffer_length_
//   [4 字节尾部对齐填充]        28  C++ 侧 struct 按 8 字节对齐,sizeof(NetTlvHeader)=32,
//                                 收发均按 32 字节头 memcpy,填充内容被忽略,置 0 即可
//
// 注意:render 接收侧校验 this_buffer_length_ <= 实际包长 - 32,
// 并按该长度拷贝 payload,所以这里必须填 payload 长度而不是头+payload。

export const TLV_HEADER_SIZE = 32
export const kNetTlvFull = 0x01
export const kNetTlvBegin = 0x02
export const kNetTlvCenter = 0x03
export const kNetTlvEnd = 0x04

export function packTlv(payload: Uint8Array, pktIndex: bigint): ArrayBuffer {
  const buf = new ArrayBuffer(TLV_HEADER_SIZE + payload.length)
  const view = new DataView(buf)
  view.setUint32(0, kNetTlvFull, true)
  view.setUint32(4, payload.length, true)
  view.setUint32(8, 0, true)
  view.setUint32(12, payload.length, true)
  view.setBigUint64(16, pktIndex, true)
  view.setUint32(24, payload.length, true)
  new Uint8Array(buf, TLV_HEADER_SIZE).set(payload)
  return buf
}

// 解包(仅支持整包 type=1);非法返回 null
export function unpackTlv(buf: ArrayBuffer): { pktIndex: bigint; payload: Uint8Array } | null {
  if (buf.byteLength < TLV_HEADER_SIZE) return null
  const view = new DataView(buf)
  const type = view.getUint32(0, true)
  if (type !== kNetTlvFull) return null
  const len = view.getUint32(4, true)
  if (len > buf.byteLength - TLV_HEADER_SIZE) return null
  return {
    pktIndex: view.getBigUint64(16, true),
    payload: new Uint8Array(buf, TLV_HEADER_SIZE, len),
  }
}

interface TlvPacket {
  type: number
  length: number
  begin: number
  parentLength: number
  pktIndex: bigint
  payload: Uint8Array
}

function parseTlvPacket(buf: ArrayBuffer): TlvPacket | null {
  if (buf.byteLength < TLV_HEADER_SIZE) return null
  const view = new DataView(buf)
  const len = view.getUint32(4, true)
  if (len > buf.byteLength - TLV_HEADER_SIZE) return null
  return {
    type: view.getUint32(0, true),
    length: len,
    begin: view.getUint32(8, true),
    parentLength: view.getUint32(24, true),
    pktIndex: view.getBigUint64(16, true),
    payload: new Uint8Array(buf, TLV_HEADER_SIZE, len),
  }
}

// ft_data_channel 接收侧重组器:
// render 对 >128KB 的消息按 Begin/Center/End 分片发送(rtc_data_channel.cpp SendData),
// 这里按 this_buffer_begin_ 偏移拼回完整 payload。datachannel 有序可靠,正常按序到达;
// 为稳妥起见缓存未到齐的分片,收满 parent_buffer_length_ 才投递。
export class TlvReassembler {
  private fragBuf: Uint8Array | null = null
  private fragReceived = 0

  // 喂入一个 datachannel 消息,返回所有重组完成的 payload(通常 0 或 1 个)
  feed(buf: ArrayBuffer): Uint8Array[] {
    const out: Uint8Array[] = []
    const pkt = parseTlvPacket(buf)
    if (!pkt) return out

    if (pkt.type === kNetTlvFull) {
      // 整包到来时若有残留分片,说明前一条消息已损坏,丢弃
      this.reset()
      out.push(pkt.payload)
      return out
    }

    if (pkt.type !== kNetTlvBegin && pkt.type !== kNetTlvCenter && pkt.type !== kNetTlvEnd) {
      return out
    }

    if (pkt.type === kNetTlvBegin || !this.fragBuf || this.fragBuf.length !== pkt.parentLength) {
      this.fragBuf = new Uint8Array(pkt.parentLength)
      this.fragReceived = 0
    }
    if (pkt.begin + pkt.length > this.fragBuf.length) {
      this.reset()
      return out
    }
    this.fragBuf.set(pkt.payload, pkt.begin)
    this.fragReceived += pkt.length
    if (this.fragReceived >= this.fragBuf.length) {
      out.push(this.fragBuf)
      this.reset()
    }
    return out
  }

  private reset() {
    this.fragBuf = null
    this.fragReceived = 0
  }
}
