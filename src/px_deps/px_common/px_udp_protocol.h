//
// Created by RGAA on 12/08/2026.
// GameStream-style UDP media protocol (custom, NOT wire-compatible with GameStream).
// Shared by render (net_udp plugin) and client (px_client_sdk).
// See docs/udp_gamestream_channel_plan.md
//

#ifndef PX_UDP_PROTOCOL_H
#define PX_UDP_PROTOCOL_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "data.h"
#include "px_fec.h"

namespace px
{

    // ---------------- wire format (little-endian) ----------------
    //
    // Common header (4B): magic(u16)='GU' | version(u8) | pkt_type(u8)
    //
    // Video shard (pkt_type=1), base header 20B:
    //   frame_index(u32) | timestamp_ms(u32) | flags(u8) | fec_block(u8) |
    //   data_shards(u16) | parity_shards(u16) | shard_index(u16) | payload_len(u16) |
    //   mon_slot(u8) | codec(u8)
    // SOF extension (only when flags & kFlagSof), right after base header:
    //   frame_width(u16) | frame_height(u16) | frame_size(u32) | mon_name_len(u8) | mon_name bytes
    // then payload(payload_len bytes)
    //
    // FEC (P2, Reed-Solomon): 每个数据 shard 的"SOF扩展+载荷"区(首包)或"载荷"区(其余包)
    // 都视为等长 P = mtu - 24 字节的保护块(末尾不足零填充,wire 上仍只发实际字节);
    // parity 包 = 基础头(flags=kFlagParity, shard_index = data_shards + j,
    // payload_len = P, 无 SOF 扩展) + P 字节校验块,整包正好 mtu。
    // 一帧一个 FEC 块,fec_block 恒 0;parity_shards 填实际值(0 = 无 FEC)。
    //
    // Audio packet (pkt_type=2), 10B header after common:
    //   seq(u32) | timestamp_ms(u32) | payload_len(u16) | Opus payload
    // 50pps(20ms 一帧),客户端经 PxUdpAudioJitterBuffer 按序交付、缺口走 Opus PLC
    //
    // Ctrl packet (pkt_type=3): subtype(u8) + body
    //   kCtrlHello(1):     association_len(u8)+association | stream_id_len(u8)+stream_id
    //   kCtrlHeartbeat(2): association_len(u8)+association
    //   kCtrlIdrRequest(3):mon_name_len(u8)+mon_name   (empty = all monitors)
    //   kCtrlFrameStatus(4): frame_index(u32) | received(u16) | lost(u16)
    //   kCtrlKick(5):      reason_len(u8)+reason   (render -> client, e.g. taken over)

    class PxUdpProtocol {
    public:
        static constexpr uint16_t kMagic = 0x4755; // 'GU'
        static constexpr uint8_t kVersion = 1;

        static constexpr uint8_t kPktVideo = 1;
        static constexpr uint8_t kPktAudio = 2;
        static constexpr uint8_t kPktCtrl = 3;

        static constexpr uint8_t kFlagKey = 0x1;
        static constexpr uint8_t kFlagSof = 0x2;
        static constexpr uint8_t kFlagEof = 0x4;
        static constexpr uint8_t kFlagParity = 0x8;
        static constexpr uint8_t kFlagRfiRecover = 0x10;

        static constexpr uint8_t kCodecH264 = 0;
        static constexpr uint8_t kCodecH265 = 1;

        static constexpr uint8_t kCtrlHello = 1;
        static constexpr uint8_t kCtrlHeartbeat = 2;
        static constexpr uint8_t kCtrlIdrRequest = 3;
        static constexpr uint8_t kCtrlFrameStatus = 4;
        static constexpr uint8_t kCtrlKick = 5;
        static constexpr uint8_t kCtrlIdrKeepalive = 6;
        static constexpr uint8_t kCtrlRfi = 7;

        static constexpr int kCommonHeaderSize = 4;
        static constexpr int kVideoHeaderSize = 20;
        static constexpr int kDefaultMtu = 1400;
        static constexpr int kWanMtu = 1024;
        static constexpr int kMaxMonNameLen = 64;

        // ---- little-endian read/write helpers ----
        static bool W16(std::span<char> output, std::size_t offset, uint16_t value) {
            if (output.size() - std::min(offset, output.size()) < sizeof(value)) return false;
            output[offset] = static_cast<char>(value & 0xff);
            output[offset + 1] = static_cast<char>((value >> 8) & 0xff);
            return true;
        }

        static bool W32(std::span<char> output, std::size_t offset, uint32_t value) {
            if (output.size() - std::min(offset, output.size()) < sizeof(value)) return false;
            output[offset] = static_cast<char>(value & 0xff);
            output[offset + 1] = static_cast<char>((value >> 8) & 0xff);
            output[offset + 2] = static_cast<char>((value >> 16) & 0xff);
            output[offset + 3] = static_cast<char>((value >> 24) & 0xff);
            return true;
        }

        static uint16_t R16(std::span<const char> input, std::size_t offset = 0) {
            if (input.size() - std::min(offset, input.size()) < sizeof(uint16_t)) return 0;
            return static_cast<uint16_t>(static_cast<uint8_t>(input[offset]) | (static_cast<uint8_t>(input[offset + 1]) << 8));
        }

        static uint32_t R32(std::span<const char> input, std::size_t offset = 0) {
            if (input.size() - std::min(offset, input.size()) < sizeof(uint32_t)) return 0;
            return static_cast<uint32_t>(static_cast<uint8_t>(input[offset]) | (static_cast<uint8_t>(input[offset + 1]) << 8) |
                                         (static_cast<uint8_t>(input[offset + 2]) << 16) |
                                         (static_cast<uint32_t>(static_cast<uint8_t>(input[offset + 3])) << 24));
        }

        static bool WriteCommon(std::span<char> output, uint8_t pkt_type) {
            if (output.size() < kCommonHeaderSize || !W16(output, 0, kMagic)) return false;
            output[2] = static_cast<char>(kVersion);
            output[3] = static_cast<char>(pkt_type);
            return true;
        }

        // returns pkt_type (>0) when valid, 0 otherwise
        static uint8_t ParseCommon(std::span<const char> data) {
            if (data.size() < kCommonHeaderSize) return 0;
            if (R16(data) != kMagic) return 0;
            if (static_cast<uint8_t>(data[2]) != kVersion) return 0;
            const auto t = static_cast<uint8_t>(data[3]);
            if (t < kPktVideo || t > kPktCtrl) return 0;
            return t;
        }

        // ---- video shard ----
        struct VideoShardInfo {
            uint32_t frame_index_ = 0;
            uint32_t timestamp_ms_ = 0;
            uint8_t flags_ = 0;
            uint8_t fec_block_ = 0;
            uint16_t data_shards_ = 0;
            uint16_t parity_shards_ = 0;
            uint16_t shard_index_ = 0;
            uint16_t payload_len_ = 0;
            uint8_t mon_slot_ = 0;
            uint8_t codec_ = 0;
            // SOF extension
            uint16_t frame_width_ = 0;
            uint16_t frame_height_ = 0;
            uint32_t frame_size_ = 0;   // 编码帧原始字节数(接收端据此精确截断,去掉 FEC 零填充)
            std::string mon_name_;
            // payload view into the original packet; valid only while that packet remains alive.
            std::span<const char> payload_{};
        };

        // parse a full UDP packet (including common header) as a video shard
        static bool ParseVideoShard(std::span<const char> data, VideoShardInfo& out) {
            if (ParseCommon(data) != kPktVideo) return false;
            if (data.size() < kCommonHeaderSize + kVideoHeaderSize) return false;
            const auto header = data.subspan(kCommonHeaderSize, kVideoHeaderSize);
            out.frame_index_ = R32(header);
            out.timestamp_ms_ = R32(header, 4);
            out.flags_ = static_cast<uint8_t>(header[8]);
            out.fec_block_ = static_cast<uint8_t>(header[9]);
            out.data_shards_ = R16(header, 10);
            out.parity_shards_ = R16(header, 12);
            out.shard_index_ = R16(header, 14);
            out.payload_len_ = R16(header, 16);
            out.mon_slot_ = static_cast<uint8_t>(header[18]);
            out.codec_ = static_cast<uint8_t>(header[19]);
            size_t off = kCommonHeaderSize + kVideoHeaderSize;
            if (out.flags_ & kFlagSof) {
                if (data.size() < off + 9) return false;
                const auto extension = data.subspan(off);
                out.frame_width_ = R16(extension);
                out.frame_height_ = R16(extension, 2);
                out.frame_size_ = R32(extension, 4);
                const auto nl = static_cast<uint8_t>(extension[8]);
                if (nl > kMaxMonNameLen || data.size() < off + 9 + nl) return false;
                out.mon_name_.assign(extension.begin() + 9, extension.begin() + 9 + nl);
                off += 9 + nl;
            }
            if (data.size() != off + out.payload_len_) return false;
            out.payload_ = data.subspan(off, out.payload_len_);
            return true;
        }

        struct VideoFrameMeta {
            uint32_t frame_index_ = 0;
            uint32_t timestamp_ms_ = 0;
            bool key_ = false;
            uint8_t codec_ = kCodecH264;
            uint16_t frame_width_ = 0;
            uint16_t frame_height_ = 0;
            uint8_t mon_slot_ = 0;
            std::string mon_name_;
            // 标记该帧是 RFI 参考帧失效后的第一个可解码 P 帧
            bool rfi_recover_ = false;
        };

        // split one encoded frame into UDP packets (<= mtu each)
        // fec_percent > 0 时按 RS(D, parity) 追加 parity 包;parity = max(1, ceil(D*fec_percent/100)),
        // D + parity > 255 时本帧退化为无 FEC。fec_percent == 0 时行为与旧版一致(仅 SOF 扩展多 frame_size)。
        static std::vector<std::shared_ptr<Data>> ShardVideoFrame(const VideoFrameMeta& meta, std::span<const char> data,
                                                                  int mtu = kDefaultMtu, int fec_percent = 0) {
            std::vector<std::shared_ptr<Data>> out;
            if (data.empty() || meta.mon_name_.size() > kMaxMonNameLen) return out;
            if (data.size() > 0xffffffff) return out;
            const int sof_ext = 9 + (int)meta.mon_name_.size();
            const int base = kCommonHeaderSize + kVideoHeaderSize;
            const int block_p = mtu - base;          // FEC 保护块大小:扩展+载荷区,所有 shard 等长
            const int first_payload = block_p - sof_ext;
            if (first_payload <= 0) return out;
            size_t total = (data.size() <= (size_t)first_payload)
                               ? 1
                               : 1 + (data.size() - first_payload + block_p - 1) / block_p;
            if (total > 0xffff) return out; // frame way too large, give up

            // parity 数;超 RS 上限则本帧不做 FEC(parity_shards 写 0,退化为现状)
            int parity_count = 0;
            if (fec_percent > 0) {
                parity_count = std::max(1, (int)((total * (size_t)fec_percent + 99) / 100));
                if (total + parity_count > DATA_SHARDS_MAX) {
                    parity_count = 0;
                }
            }

            // pass 1: 逐 shard 生成 P 字节保护块(shard 0 = SOF扩展+载荷,其余 = 载荷,末尾零填充)
            std::vector<std::string> blocks(total, std::string(block_p, '\0'));
            size_t sent = 0;
            for (size_t i = 0; i < total; i++) {
                bool sof = (i == 0);
                int payload_cap = sof ? first_payload : block_p;
                int plen = (int)std::min<size_t>(payload_cap, data.size() - sent);
                auto block = std::span<char>{blocks[i]};
                size_t off = 0;
                if (sof) {
                    W16(block, 0, meta.frame_width_);
                    W16(block, 2, meta.frame_height_);
                    W32(block, 4, static_cast<uint32_t>(data.size()));
                    block[8] = static_cast<char>(meta.mon_name_.size());
                    std::ranges::copy(meta.mon_name_, block.begin() + 9);
                    off += sof_ext;
                }
                std::ranges::copy(data.subspan(sent, plen), block.begin() + static_cast<std::ptrdiff_t>(off));
                sent += plen;
            }

            // pass 2: RS 编码生成 parity 块;失败则整帧退化为无 FEC
            std::vector<std::string> parity;
            if (parity_count > 0) {
                parity = PxFec::Encode(blocks, parity_count);
                if ((int)parity.size() != parity_count) {
                    parity_count = 0;
                    parity.clear();
                }
            }

            // pass 3: 组包(数据包在前、parity 包随后,与 Sunshine 顺序一致);
            // wire 上数据包只发实际字节(不含零填充),parity 包整包正好 mtu
            out.reserve(total + parity.size());
            sent = 0;
            for (size_t i = 0; i < total; i++) {
                bool sof = (i == 0);
                bool eof = (i == total - 1);
                int payload_cap = sof ? first_payload : block_p;
                int plen = (int)std::min<size_t>(payload_cap, data.size() - sent);
                size_t pkt_size = base + (sof ? sof_ext : 0) + plen;
                auto buf = Data::Allocate(pkt_size);
                auto packet = buf->MutableBytes();
                WriteCommon(packet, kPktVideo);
                auto header = packet.subspan(kCommonHeaderSize, kVideoHeaderSize);
                W32(header, 0, meta.frame_index_);
                W32(header, 4, meta.timestamp_ms_);
                uint8_t flags = (meta.key_ ? kFlagKey : 0) | (sof ? kFlagSof : 0) | (eof ? kFlagEof : 0) |
                                (meta.rfi_recover_ ? kFlagRfiRecover : 0);
                header[8] = static_cast<char>(flags);
                header[9] = 0; // fec_block,一帧一块,恒 0
                W16(header, 10, static_cast<uint16_t>(total));
                W16(header, 12, static_cast<uint16_t>(parity_count));
                W16(header, 14, static_cast<uint16_t>(i));
                W16(header, 16, static_cast<uint16_t>(plen));
                header[18] = static_cast<char>(meta.mon_slot_);
                header[19] = static_cast<char>(meta.codec_);
                // 包体 = 保护块前缀(扩展+实际载荷),与 blocks[i] 一致
                const auto body_size = static_cast<std::size_t>((sof ? sof_ext : 0) + plen);
                std::ranges::copy(std::span<const char>{blocks[i]}.first(body_size), packet.begin() + base);
                sent += plen;
                out.push_back(buf);
            }
            for (size_t j = 0; j < parity.size(); j++) {
                auto buf = Data::Allocate(base + block_p);
                auto packet = buf->MutableBytes();
                WriteCommon(packet, kPktVideo);
                auto header = packet.subspan(kCommonHeaderSize, kVideoHeaderSize);
                W32(header, 0, meta.frame_index_);
                W32(header, 4, meta.timestamp_ms_);
                header[8] = static_cast<char>(kFlagParity | (meta.key_ ? kFlagKey : 0));
                header[9] = 0;
                W16(header, 10, static_cast<uint16_t>(total));
                W16(header, 12, static_cast<uint16_t>(parity_count));
                W16(header, 14, static_cast<uint16_t>(total + j));
                W16(header, 16, static_cast<uint16_t>(block_p));
                header[18] = static_cast<char>(meta.mon_slot_);
                header[19] = static_cast<char>(meta.codec_);
                std::ranges::copy(parity[j], packet.begin() + base);
                out.push_back(buf);
            }
            return out;
        }

        // ---- audio packet ----
        static constexpr int kAudioHeaderSize = 10; // seq(4) | timestamp_ms(4) | payload_len(2)

        struct AudioPacketInfo {
            uint32_t seq_ = 0;
            uint32_t timestamp_ms_ = 0;
            uint16_t payload_len_ = 0;
            // payload view into the original packet; valid only while that packet remains alive.
            std::span<const char> payload_{};
        };

        static std::shared_ptr<Data> BuildAudioPacket(uint32_t seq, uint32_t timestamp_ms, std::span<const char> payload) {
            if (payload.empty() || payload.size() > 0xffff) return nullptr;
            size_t n = kCommonHeaderSize + kAudioHeaderSize + payload.size();
            auto buf = Data::Allocate(n);
            auto packet = buf->MutableBytes();
            WriteCommon(packet, kPktAudio);
            auto header = packet.subspan(kCommonHeaderSize, kAudioHeaderSize);
            W32(header, 0, seq);
            W32(header, 4, timestamp_ms);
            W16(header, 8, static_cast<uint16_t>(payload.size()));
            std::ranges::copy(payload, packet.begin() + kCommonHeaderSize + kAudioHeaderSize);
            return buf;
        }

        // parse a full UDP packet (including common header) as an audio packet
        static bool ParseAudioPacket(std::span<const char> data, AudioPacketInfo& out) {
            if (ParseCommon(data) != kPktAudio) return false;
            if (data.size() < kCommonHeaderSize + kAudioHeaderSize) return false;
            const auto header = data.subspan(kCommonHeaderSize, kAudioHeaderSize);
            out.seq_ = R32(header);
            out.timestamp_ms_ = R32(header, 4);
            out.payload_len_ = R16(header, 8);
            if (data.size() != kCommonHeaderSize + kAudioHeaderSize + out.payload_len_) return false;
            out.payload_ = data.subspan(kCommonHeaderSize + kAudioHeaderSize, out.payload_len_);
            return true;
        }

        // ---- ctrl builders ----
        static std::shared_ptr<Data> BuildCtrlString2(uint8_t subtype, const std::string& a, const std::string& b) {
            size_t n = kCommonHeaderSize + 1 + 1 + a.size() + 1 + b.size();
            if (a.size() > 0xff || b.size() > 0xff) return nullptr;
            auto buf = Data::Allocate(n);
            auto packet = buf->MutableBytes();
            WriteCommon(packet, kPktCtrl);
            packet[kCommonHeaderSize] = static_cast<char>(subtype);
            auto offset = static_cast<std::size_t>(kCommonHeaderSize + 1);
            packet[offset++] = static_cast<char>(a.size());
            std::ranges::copy(a, packet.begin() + static_cast<std::ptrdiff_t>(offset));
            offset += a.size();
            packet[offset++] = static_cast<char>(b.size());
            std::ranges::copy(b, packet.begin() + static_cast<std::ptrdiff_t>(offset));
            return buf;
        }

        static std::shared_ptr<Data> BuildCtrlString1(uint8_t subtype, const std::string& a) {
            size_t n = kCommonHeaderSize + 1 + 1 + a.size();
            if (a.size() > 0xff) return nullptr;
            auto buf = Data::Allocate(n);
            auto packet = buf->MutableBytes();
            WriteCommon(packet, kPktCtrl);
            packet[kCommonHeaderSize] = static_cast<char>(subtype);
            const auto offset = static_cast<std::size_t>(kCommonHeaderSize + 1);
            packet[offset] = static_cast<char>(a.size());
            std::ranges::copy(a, packet.begin() + static_cast<std::ptrdiff_t>(offset + 1));
            return buf;
        }

        static std::shared_ptr<Data> BuildHello(const std::string& association_code,
                                                const std::string& stream_id) {
            return BuildCtrlString2(kCtrlHello, association_code, stream_id);
        }
        static std::shared_ptr<Data> BuildHeartbeat(const std::string& association_code) {
            return BuildCtrlString1(kCtrlHeartbeat, association_code);
        }
        static std::shared_ptr<Data> BuildIdrRequest(const std::string& mon_name) {
            return BuildCtrlString1(kCtrlIdrRequest, mon_name);
        }
        // 连接初始化 / 长时间无帧时的软请求:语义与 IDR 请求相同,但 render 不计入
        // 动态 FEC 的丢帧窗口,避免自动补关键帧把 fec 刷到上限。
        static std::shared_ptr<Data> BuildIdrKeepalive(const std::string& mon_name) {
            return BuildCtrlString1(kCtrlIdrKeepalive, mon_name);
        }
        // RFI(参考帧失效):s1 = 失效参考帧的 frame_index(字符串),s2 = mon_name(空=全屏)。
        // 与 Moonlight 的 URGENT RFI 语义一致:render 优先让编码器跳过坏参考帧,不插 IDR。
        static std::shared_ptr<Data> BuildRfi(uint64_t invalid_frame_index, const std::string& mon_name) {
            return BuildCtrlString2(kCtrlRfi, std::to_string(invalid_frame_index), mon_name);
        }
        static std::shared_ptr<Data> BuildKick(const std::string& reason) {
            return BuildCtrlString1(kCtrlKick, reason);
        }

        // kCtrlFrameStatus: frame_index(u32) | received(u16) | lost(u16),定长二进制
        // received/lost 语义见 PxUdpFrameReassembler::on_frame_status_
        static std::shared_ptr<Data> BuildFrameStatus(uint32_t frame_index, uint16_t received, uint16_t lost) {
            size_t n = kCommonHeaderSize + 1 + 8;
            auto buf = Data::Allocate(n);
            auto packet = buf->MutableBytes();
            WriteCommon(packet, kPktCtrl);
            packet[kCommonHeaderSize] = static_cast<char>(kCtrlFrameStatus);
            const auto body = packet.subspan(kCommonHeaderSize + 1);
            W32(body, 0, frame_index);
            W16(body, 4, received);
            W16(body, 6, lost);
            return buf;
        }

        // ParseCtrl 不解析 kCtrlFrameStatus(非字符串体),走这个定长解析
        static bool ParseFrameStatus(std::span<const char> data, uint32_t& frame_index, uint16_t& received, uint16_t& lost) {
            if (ParseCommon(data) != kPktCtrl) return false;
            if (data.size() != kCommonHeaderSize + 1 + 8) return false;
            if ((uint8_t)data[kCommonHeaderSize] != kCtrlFrameStatus) return false;
            const auto body = data.subspan(kCommonHeaderSize + 1);
            frame_index = R32(body);
            received = R16(body, 4);
            lost = R16(body, 6);
            return true;
        }

        // parse ctrl packet body; returns subtype(>0) or 0.
        // strings are filled for Hello(device_id,stream_id) / Heartbeat(stream_id) /
        // IdrRequest(mon_name) / Kick(reason).
        static uint8_t ParseCtrl(std::span<const char> data, std::string& s1, std::string& s2) {
            if (ParseCommon(data) != kPktCtrl) return 0;
            if (data.size() < kCommonHeaderSize + 1) return 0;
            uint8_t subtype = (uint8_t)data[kCommonHeaderSize];
            std::size_t offset = kCommonHeaderSize + 1;
            auto read_str = [&](std::string& out) -> bool {
                if (offset >= data.size()) return false;
                const auto length = static_cast<uint8_t>(data[offset++]);
                if (data.size() - offset < length) return false;
                out.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + length));
                offset += length;
                return true;
            };
            s1.clear(); s2.clear();
            switch (subtype) {
                case kCtrlHello:
                    if (!read_str(s1) || !read_str(s2)) return 0;
                    return subtype;
                case kCtrlHeartbeat:
                case kCtrlIdrRequest:
                case kCtrlIdrKeepalive:
                case kCtrlKick:
                    if (!read_str(s1)) return 0;
                    return subtype;
                case kCtrlRfi:
                    if (!read_str(s1) || !read_str(s2)) return 0;
                    return subtype;
                default:
                    return 0;
            }
        }
    };

    // ---------------- client-side frame reassembler ----------------
    //
    // Collects video shards (per mon_slot), emits complete frames.
    // FEC (P2): slot 扩到 data_shards + parity_shards,统一存 P 字节"保护块"
    // (shard 0 = SOF扩展+载荷,其余数据块 = 载荷,parity 块 = 载荷,wire 上不足 P 的零填充);
    // 已收 distinct 块数(数据+parity)达到 data_shards 且有数据块缺失时立刻 RS 恢复,
    // 重组帧按 SOF 扩展里的 frame_size 精确截断(去掉零填充)。
    // Loss policy: a newer frame_index for the same mon_slot declares the in-progress
    // frame lost (recovery attempted first). For FEC frames, receipt of EOF proves
    // the sender has emitted every data shard; if the missing-data count already
    // exceeds the entire parity budget, declare that frame lost immediately instead
    // of waiting for the next frame. After any loss, P frames are dropped until a key
    // frame completes (mirrors the webrtc_local convention that the first delivered
    // frame must be an IDR).
    class PxUdpFrameReassembler {
    public:
        struct CompleteFrame {
            uint8_t mon_slot_ = 0;
            std::string mon_name_;
            uint32_t frame_index_ = 0;
            uint32_t timestamp_ms_ = 0;
            bool key_ = false;
            bool rfi_recover_ = false;
            uint8_t codec_ = 0;
            uint16_t frame_width_ = 0;
            uint16_t frame_height_ = 0;
            std::shared_ptr<Data> data_;
        };

        // completed, decodable frame
        std::function<void(const CompleteFrame&)> on_frame_;
        // a frame was declared lost (gap); client should request an IDR for this slot
        std::function<void(uint8_t mon_slot, uint32_t lost_frame_index)> on_frame_lost_;
        // 帧状态反馈(每帧恰好一次,驱动 render 端动态 FEC):
        // 完成帧 received=网络实收数据块数(FEC 恢复的不算), lost=经 FEC 恢复的数据块数;
        // 判丢帧 received=已收数据块数, lost=缺失数据块数
        std::function<void(uint8_t mon_slot, uint32_t frame_index, uint16_t received, uint16_t lost)> on_frame_status_;

        // 新连接/断线重连时清空跨连接状态。render 重启或接管后 frame_index 会回退,
        // 不清空会把新连接的所有包当成“迟到旧包”丢到序列追上为止。
        void Reset() {
            assemblies_.clear();
            need_key_.clear();
            finished_.clear();
        }

        // RS 恢复后的 sanity check(防误恢复的坏数据进解码器):
        // mon_name_len 合法且 frame_size 在容量内(sof_ext + frame_size <= data_shards * P)。
        // 仅「有 parity 参与的恢复」路径需要,纯数据收齐的块经过 ParseVideoShard 校验不用查
        static bool ValidateRecoveredShard0(const std::string& b0, int data_shards, size_t p) {
            if (b0.size() < 9) return false;
            uint8_t nl = (uint8_t)b0[8];
            if (nl > PxUdpProtocol::kMaxMonNameLen) return false;
            size_t sof_ext = 9 + nl;
            if (b0.size() < sof_ext) return false;
            uint32_t frame_size = PxUdpProtocol::R32(std::span<const char>{b0}, 4);
            // 容量:shard 0 载荷 P - sof_ext,其余 D-1 块各 P ⟺ sof_ext + frame_size <= D * P
            return (uint64_t)sof_ext + frame_size <= (uint64_t)data_shards * p;
        }

        // feed one raw UDP packet (common header included)
        void AddPacket(std::span<const char> data) {
            PxUdpProtocol::VideoShardInfo shard;
            if (!PxUdpProtocol::ParseVideoShard(data, shard)) return;
            if (shard.data_shards_ == 0) return;
            const bool is_parity = (shard.flags_ & PxUdpProtocol::kFlagParity) != 0;
            if (is_parity) {
                if (shard.parity_shards_ == 0) return;
                if (shard.shard_index_ < shard.data_shards_ ||
                    shard.shard_index_ >= shard.data_shards_ + shard.parity_shards_) return;
            }
            else if (shard.shard_index_ >= shard.data_shards_) {
                return;
            }
            // 已完成/已判丢帧的迟到包(含恢复后晚到的 parity)直接丢
            auto fit = finished_.find(shard.mon_slot_);
            if (fit != finished_.end() && shard.frame_index_ <= fit->second) {
                // render 编码器在重连/接管后 frame_index 可能整体回退(本次实测 836 → 63)。
                // 新流的首包是 SOF+key,把它当成新流并清掉该 mon_slot 的旧水位,而不是继续丢包。
                bool new_stream = (shard.flags_ & PxUdpProtocol::kFlagSof) &&
                                  (shard.flags_ & PxUdpProtocol::kFlagKey) &&
                                  shard.frame_index_ < fit->second;
                if (!new_stream) return;
                assemblies_.erase(shard.mon_slot_);
                need_key_.erase(shard.mon_slot_);
                finished_.erase(shard.mon_slot_);
                fit = finished_.end();
            }

            auto& cur = assemblies_[shard.mon_slot_];
            if (cur.active_ && shard.frame_index_ > cur.frame_index_) {
                // newer frame arrived while current incomplete -> try recovery, then declare loss
                if (!TryRecoverAndEmit(shard.mon_slot_, cur)) {
                    DeclareLoss(shard.mon_slot_, cur.frame_index_,
                                (uint16_t)cur.net_data_received_,
                                (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                    MarkFinished(shard.mon_slot_, cur.frame_index_);
                }
                cur = Assembly{};
            }
            if (cur.active_ && shard.frame_index_ < cur.frame_index_) {
                return; // stale shard of an already-lost/completed frame
            }
            if (!cur.active_) {
                // 整帧丢失检测:finished_ 之后、本帧之前若有帧号空缺,说明中间帧所有包全丢,
                // 「cur.active_ 时更大帧号到达」的判丢路径不会触发,必须在这里补上,
                // 否则无限 GOP 下解码器继续吃参考链已断的 P 帧 → 花屏。
                // 迟到乱序包已被上面的 finished_ 检查拦截,不会误判;首连(finished_ 不存在)不触发
                if (fit != finished_.end() && shard.frame_index_ > fit->second + 1) {
                    DeclareLoss(shard.mon_slot_, shard.frame_index_ - 1, 0, 0);
                    MarkFinished(shard.mon_slot_, shard.frame_index_ - 1);
                }
                if (shard.flags_ & PxUdpProtocol::kFlagSof) {
                    cur = Assembly{};
                    cur.active_ = true;
                    cur.meta_ready_ = true;
                    cur.frame_index_ = shard.frame_index_;
                    cur.timestamp_ms_ = shard.timestamp_ms_;
                    cur.key_ = (shard.flags_ & PxUdpProtocol::kFlagKey) != 0;
                    cur.rfi_recover_ = (shard.flags_ & PxUdpProtocol::kFlagRfiRecover) != 0;
                    cur.codec_ = shard.codec_;
                    cur.frame_width_ = shard.frame_width_;
                    cur.frame_height_ = shard.frame_height_;
                    cur.mon_name_ = shard.mon_name_;
                    cur.data_shards_ = shard.data_shards_;
                    cur.shards_.resize((size_t)shard.data_shards_ + shard.parity_shards_);
                    cur.received_ = 0;
                }
                else if (shard.parity_shards_ > 0) {
                    // FEC 帧的 SOF 丢了:先从数据/parity 包建起组装,元信息等 shard 0 恢复后取
                    cur = Assembly{};
                    cur.active_ = true;
                    cur.meta_ready_ = false;
                    cur.frame_index_ = shard.frame_index_;
                    cur.timestamp_ms_ = shard.timestamp_ms_;
                    cur.key_ = (shard.flags_ & PxUdpProtocol::kFlagKey) != 0;
                    cur.rfi_recover_ = (shard.flags_ & PxUdpProtocol::kFlagRfiRecover) != 0;
                    cur.codec_ = shard.codec_;
                    cur.data_shards_ = shard.data_shards_;
                    cur.shards_.resize((size_t)shard.data_shards_ + shard.parity_shards_);
                    cur.received_ = 0;
                }
                else {
                    // joining mid-frame (no FEC): we cannot trust earlier shards, treat as broken
                    DeclareLoss(shard.mon_slot_, shard.frame_index_, 0, shard.data_shards_);
                    MarkFinished(shard.mon_slot_, shard.frame_index_);
                    return;
                }
            }
            // same frame
            if (cur.shards_.size() != (size_t)shard.data_shards_ + shard.parity_shards_ ||
                cur.data_shards_ != shard.data_shards_) {
                // inconsistent shard count, frame is broken
                DeclareLoss(shard.mon_slot_, cur.frame_index_,
                            (uint16_t)cur.net_data_received_,
                            (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                MarkFinished(shard.mon_slot_, cur.frame_index_);
                cur = Assembly{};
                return;
            }
            auto& slot = cur.shards_[shard.shard_index_];
            if (!slot.filled_) {
                slot.filled_ = true;
                if ((shard.flags_ & PxUdpProtocol::kFlagSof) && !is_parity) {
                    // SOF 数据块:保护块 = SOF 扩展 + 载荷
                    size_t ext_len = 9 + shard.mon_name_.size();
                    const auto extension = data.subspan(PxUdpProtocol::kCommonHeaderSize + PxUdpProtocol::kVideoHeaderSize,
                                                        ext_len + shard.payload_len_);
                    slot.bytes_.assign(extension.begin(), extension.end());
                }
                else {
                    slot.bytes_.assign(shard.payload_.begin(), shard.payload_.end());
                }
                cur.received_++;
                if (!is_parity) cur.net_data_received_++;
            }
            if (shard.flags_ & PxUdpProtocol::kFlagSof) {
                cur.meta_ready_ = true;
                cur.mon_name_ = shard.mon_name_;
                cur.frame_width_ = shard.frame_width_;
                cur.frame_height_ = shard.frame_height_;
                cur.rfi_recover_ = (shard.flags_ & PxUdpProtocol::kFlagRfiRecover) != 0;
            }

            int data_filled = 0;
            for (int i = 0; i < cur.data_shards_; i++) {
                if (cur.shards_[i].filled_) data_filled++;
            }
            if (data_filled == cur.data_shards_) {
                CompleteWithStatus(shard.mon_slot_, cur);
                MarkFinished(shard.mon_slot_, cur.frame_index_);
                cur = Assembly{};
            }
            else if ((shard.flags_ & PxUdpProtocol::kFlagEof) &&
                     cur.shards_.size() > (size_t)cur.data_shards_ &&
                     data_filled + (int)(cur.shards_.size() - cur.data_shards_) < cur.data_shards_) {
                // EOF 是发送端已发完所有数据 shard 的明确边界。此时即便后续所有 parity
                // 都到达，当前缺失的数据块仍超过 FEC 能恢复的上限；直接上报 RFI，不等下一帧。
                // 只在 EOF 上判定而非“首个 parity”上判定，保留 UDP 包乱序时 parity 先到、
                // 数据块随后到达的正常恢复路径。
                DeclareLoss(shard.mon_slot_, cur.frame_index_,
                            (uint16_t)cur.net_data_received_,
                            (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                MarkFinished(shard.mon_slot_, cur.frame_index_);
                cur = Assembly{};
            }
            else if ((int)cur.received_ >= cur.data_shards_ && cur.shards_.size() > (size_t)cur.data_shards_) {
                // 「够用即恢复」:distinct 块数(数据+parity)够 data_shards 且有数据块缺失
                if (TryRecoverAndEmit(shard.mon_slot_, cur)) {
                    MarkFinished(shard.mon_slot_, cur.frame_index_);
                }
                else {
                    DeclareLoss(shard.mon_slot_, cur.frame_index_,
                                (uint16_t)cur.net_data_received_,
                                (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                    MarkFinished(shard.mon_slot_, cur.frame_index_);
                }
                cur = Assembly{};
            }
        }

    private:
        struct ShardSlot {
            bool filled_ = false;
            std::string bytes_;
        };
        struct Assembly {
            bool active_ = false;
            bool meta_ready_ = false;       // shard 0(SOF)是否已确认/接收
            uint32_t frame_index_ = 0;
            uint32_t timestamp_ms_ = 0;
            bool key_ = false;
            bool rfi_recover_ = false;
            uint8_t codec_ = 0;
            uint16_t frame_width_ = 0;
            uint16_t frame_height_ = 0;
            std::string mon_name_;
            int data_shards_ = 0;           // shards_ = data + parity 个 slot
            std::vector<ShardSlot> shards_;
            size_t received_ = 0;           // distinct 已收块数(数据+parity)
            int net_data_received_ = 0;     // 网络实收数据块数(不含 parity、不含 FEC 恢复)
        };

        void FireStatus(uint8_t mon_slot, uint32_t frame_index, uint16_t received, uint16_t lost) {
            if (on_frame_status_) on_frame_status_(mon_slot, frame_index, received, lost);
        }

        void DeclareLoss(uint8_t mon_slot, uint32_t frame_index, uint16_t received, uint16_t lost) {
            need_key_[mon_slot] = true;
            if (on_frame_lost_) on_frame_lost_(mon_slot, frame_index);
            FireStatus(mon_slot, frame_index, received, lost);
        }

        void MarkFinished(uint8_t mon_slot, uint32_t frame_index) {
            auto& f = finished_[mon_slot];
            if (frame_index > f) f = frame_index;
        }

        // 完成帧:拼帧成功则触发状态(received=网络实收数据块,lost=FEC 恢复块);
        // EmitFrame 内部判丢的异常路径不重复触发(DeclareLoss 已带状态)
        void CompleteWithStatus(uint8_t mon_slot, Assembly& cur) {
            if (EmitFrame(mon_slot, cur)) {
                FireStatus(mon_slot, cur.frame_index_,
                           (uint16_t)cur.net_data_received_,
                           (uint16_t)(cur.data_shards_ - cur.net_data_received_));
            }
        }

        // 够用即恢复:缺失数据块 <= 已收 parity 块时 RS 重建;成功则 CompleteWithStatus
        bool TryRecoverAndEmit(uint8_t mon_slot, Assembly& cur) {
            if (cur.data_shards_ <= 0 || cur.shards_.size() <= (size_t)cur.data_shards_) return false;
            int data_filled = 0;
            for (int i = 0; i < cur.data_shards_; i++) {
                if (cur.shards_[i].filled_) data_filled++;
            }
            if (data_filled == cur.data_shards_) {
                CompleteWithStatus(mon_slot, cur);
                return true;
            }
            if ((int)cur.received_ < cur.data_shards_) return false;

            // 统一补齐到 P(parity 块与满数据块都是 P,仅末尾数据块可能短)
            size_t p = 0;
            for (auto& s : cur.shards_) {
                if (s.filled_ && s.bytes_.size() > p) p = s.bytes_.size();
            }
            if (p == 0) return false;
            std::vector<std::string> blocks;
            blocks.reserve(cur.shards_.size());
            for (auto& s : cur.shards_) {
                blocks.push_back(s.filled_ ? s.bytes_ : std::string{});
                if (!blocks.back().empty() && blocks.back().size() < p) {
                    blocks.back().append(p - blocks.back().size(), '\0');
                }
            }
            if (!PxFec::Decode(blocks, cur.data_shards_)) return false;
            for (size_t i = 0; i < blocks.size(); i++) {
                if (blocks[i].empty()) return false; // 没全填回,防御
                cur.shards_[i].filled_ = true;
                cur.shards_[i].bytes_ = std::move(blocks[i]);
            }
            // sanity check 恢复出的 shard 0:校验失败按恢复失败处理(调用方判丢),坏帧不进解码器
            if (!ValidateRecoveredShard0(cur.shards_[0].bytes_, cur.data_shards_, p)) return false;
            CompleteWithStatus(mon_slot, cur);
            return true;
        }

        // 拼帧:shard 0 块跳过 SOF 扩展前缀、其余取整块,拼接后按 frame_size 精确截断;
        // shard 0 缺失被恢复时,mon_name/分辨率也从恢复块里取。
        // 返回 true = 帧完成(已发出或按规则丢弃);false = 内部判丢(DeclareLoss 已触发,勿重复处理)
        bool EmitFrame(uint8_t mon_slot, Assembly& cur) {
            const std::string& b0 = cur.shards_[0].bytes_;
            if (b0.size() < 9) {
                DeclareLoss(mon_slot, cur.frame_index_,
                            (uint16_t)cur.net_data_received_,
                            (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                return false;
            }
            uint8_t nl = (uint8_t)b0[8];
            size_t ext = 9 + nl;
            if (b0.size() < ext) {
                DeclareLoss(mon_slot, cur.frame_index_,
                            (uint16_t)cur.net_data_received_,
                            (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                return false;
            }
            CompleteFrame f;
            f.mon_slot_ = mon_slot;
            f.frame_index_ = cur.frame_index_;
            f.timestamp_ms_ = cur.timestamp_ms_;
            f.key_ = cur.key_;
            f.codec_ = cur.codec_;
            const auto first_block = std::span<const char>{b0};
            f.frame_width_ = PxUdpProtocol::R16(first_block);
            f.frame_height_ = PxUdpProtocol::R16(first_block, 2);
            uint32_t frame_size = PxUdpProtocol::R32(first_block, 4);
            f.mon_name_.assign(b0.data() + 9, nl);

            f.data_ = Data::Allocate( frame_size);
            size_t off = 0;
            for (int i = 0; i < cur.data_shards_ && off < frame_size; i++) {
                const std::string& blk = cur.shards_[i].bytes_;
                auto source = std::span<const char>{blk};
                if (i == 0) {
                    if (source.size() < ext) break;
                    source = source.subspan(ext);
                }
                const auto len = std::min(source.size(), static_cast<std::size_t>(frame_size) - off);
                std::ranges::copy(source.first(len), f.data_->MutableBytes().begin() + static_cast<std::ptrdiff_t>(off));
                off += len;
            }
            if (off < frame_size) {
                // 拼不满,帧实际损坏(理论上 FEC 成功后不会发生)
                DeclareLoss(mon_slot, cur.frame_index_,
                            (uint16_t)cur.net_data_received_,
                            (uint16_t)(cur.data_shards_ - cur.net_data_received_));
                return false;
            }
            bool decodable = f.key_ || cur.rfi_recover_ || !need_key_[mon_slot];
            if (f.key_ || cur.rfi_recover_) need_key_[mon_slot] = false;
            if (decodable && on_frame_) on_frame_(f);
            return true;
        }

        std::map<uint8_t, Assembly> assemblies_;
        std::map<uint8_t, bool> need_key_;
        std::map<uint8_t, uint32_t> finished_;  // 已完成/已判丢的最大 frame_index(迟到包直接丢)
    };

    // ---------------- client-side audio jitter buffer ----------------
    //
    // 音频 50pps(20ms 一帧),按 seq 重排序交付;缺失 seq 等最新缓冲包领先超过 2 帧
    // (60ms)后通过 on_lost_ 上报,由上层喂 Opus PLC(DecodeDummy)补 20ms。
    // 无序号回绕处理(50pps 下 u32 约 2.7 年才绕一圈,回绕/对端重启走大幅回退重置)。
    //
    // 两条真机踩过的坑:
    // 1. 判丢必须看"最新"缓冲包(rbegin)而不是最老(begin):最老的包可能因乱序
    //    恰好只领先 expected_ 1~2 帧,看它会漏判;看最新的才能稳定触发 60ms 容忍窗口
    // 2. 缓冲满时绝不能淘汰"最老"(最接近 expected_)的包:expected_ 落后 3 帧以上后,
    //    每来一包删一个最老、判丢只爬 1 格,追赶速度=到达速度,expected_ 永远追不上,
    //    进入永久判丢死循环(日志 50/s 刷盘,接收线程被拖垮,视频跟着卡死)
    class PxUdpAudioJitterBuffer {
    public:
        static constexpr int kMaxBuffered = 16;        // 缓冲上限(320ms),满时丢弃超前的新包
        static constexpr int kMaxConsecutiveLost = 5;  // 单次 Drain 最多连续判丢数
        static constexpr uint32_t kResyncThreshold = 6000; // seq 大幅回退(对端重启/回绕)判为新流

        // 按序到达的音频帧:seq | timestamp_ms | Opus payload
        std::function<void(uint32_t seq, uint32_t timestamp_ms, std::span<const char> payload)> on_frame_;
        // 判定丢失的 seq(等够 60ms 仍未到),每个丢失 seq 恰好报一次
        std::function<void(uint32_t seq)> on_lost_;

        void Reset() {
            packets_.clear();
            inited_ = false;
            expected_ = 0;
        }

        void AddPacket(uint32_t seq, uint32_t timestamp_ms, std::span<const char> payload) {
            if (payload.empty()) return;
            if (!inited_) {
                // 中途加入不补历史:从首个到达包开始按序交付
                inited_ = true;
                expected_ = seq;
            }
            // 对端重启 seq 归零重来(或 u32 回绕):大幅回退视为新流,重置重新对齐
            if (seq < expected_ && expected_ - seq > kResyncThreshold) {
                packets_.clear();
                expected_ = seq;
            }
            if (seq < expected_) return; // 迟到/重复包
            // 缓冲满且新包比所有缓冲都新:丢新包,保住最接近 expected_ 的老包让它追上来;
            // 被丢的新包之后会被诚实判丢,走 PLC
            if ((int)packets_.size() >= kMaxBuffered && seq > packets_.rbegin()->first) {
                return;
            }
            packets_[seq] = Packet{timestamp_ms, std::string(payload.begin(), payload.end())};
            // 窗口内乱序插入导致的溢出:淘汰最新
            while ((int)packets_.size() > kMaxBuffered) {
                packets_.erase(std::prev(packets_.end()));
            }
            Drain();
        }

    private:
        struct Packet {
            uint32_t ts_{0};
            std::string data_{};
        };

        void Drain() {
            int lost = 0;
            for (;;) {
                auto it = packets_.find(expected_);
                if (it != packets_.end()) {
                    if (on_frame_) on_frame_(expected_, it->second.ts_, std::span<const char>{it->second.data_});
                    packets_.erase(it);
                    expected_++;
                    continue;
                }
                if (packets_.empty()) break;
                // 最新缓冲包比 expected_ 领先超过 2 帧(60ms)→ expected_ 判丢
                if (packets_.rbegin()->first > expected_ + 2 && lost < kMaxConsecutiveLost) {
                    if (on_lost_) on_lost_(expected_);
                    expected_++;
                    lost++;
                    continue;
                }
                break;
            }
        }

        std::map<uint32_t, Packet> packets_;
        bool inited_ = false;
        uint32_t expected_ = 0;
    };

}

#endif //PX_UDP_PROTOCOL_H
