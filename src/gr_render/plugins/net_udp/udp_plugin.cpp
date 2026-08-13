//
// Created RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面,见 udp_plugin.h 头注释
//

#include "udp_plugin.h"
#include <chrono>
#include <algorithm>
#include <thread>
#include "gr_render/plugins/plugin_ids.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/gr_udp_protocol.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_plugin_context.h"

namespace tc
{

    std::string UdpPlugin::GetPluginId() {
        return kNetUdpPluginId;
    }

    std::string UdpPlugin::GetPluginName() {
        return "Net UDP";
    }

    std::string UdpPlugin::GetVersionName() {
        return "1.2.0";
    }

    uint32_t UdpPlugin::GetVersionCode() {
        return 120;
    }

    std::string UdpPlugin::GetPluginDescription() {
        return "Network via UDP";
    }

    bool UdpPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrNetPlugin::OnCreate(param);
        udp_listen_port_ = (int)GetConfigIntParam("udp-listen-port");
        auto config_listen_port = (int)GetConfigIntParam("listen-port");
        if (config_listen_port > 0) {
            udp_listen_port_ = config_listen_port;
        }
        if (HasParam("fec-percent")) {
            // 0 = 关闭 FEC;缺省保持默认 20%
            fec_percent_ = (int)GetConfigIntParam("fec-percent");
        }
        if (HasParam("mtu")) {
            auto mtu = (int)GetConfigIntParam("mtu");
            if (mtu >= 576 && mtu <= 1500) {
                udp_mtu_ = mtu;
            }
        }
        configured_fec_percent_ = fec_percent_.load();
        // Windows sleep 默认 15.6ms 粒度,帧内 pacing 的 1ms sleep 需要先把计时器分辨率提到 1ms
        timeBeginPeriod(1);
        LOGI("Listen port: {}, fec percent: {}, mtu: {}, pacing: {} shards/{}ms",
             udp_listen_port_, fec_percent_.load(), udp_mtu_, kPaceChunkSize, kPaceSleepMs);
        StartInternal();

        // 心跳扫描:超 10s 无心跳的绑定会话判定掉线
        if (plugin_context_) {
            plugin_context_->StartTimer(kHeartbeatScanIntervalMs, [=, this]() {
                SweepDeadSessions();
            });
            // FRAME_STATUS 窗口:5s 一个窗口,按判丢率动态调 FEC 百分比
            plugin_context_->StartTimer(kFecWindowMs, [=, this]() {
                AdjustFecWindow();
            });
        }
        return true;
    }

    bool UdpPlugin::OnDestroy() {
        GrNetPlugin::OnStop();
        if (server_) {
            server_->stop();
            server_.reset();
        }
        sessions_.Clear();
        timeEndPeriod(1);
        return GrNetPlugin::OnDestroy();
    }

    // wire 级扫描 tc.Message,提取 kAudioFrame(40) 里 AudioFrame.data(field 5, bytes)
    // 的 Opus payload——与 ws_server.cpp 的 IsMediaFrameMessage 同一做法
    // (插件不引 protobuf 头,避免 absl 冲突,见 ws_server.cpp:73 注释)。
    // 返回 true 时 payload 指向 msg 内部缓冲,调用方需保持 msg 存活。
    static bool ExtractAudioPayload(const std::shared_ptr<Data>& msg, const char*& payload, size_t& payload_len) {
        payload = nullptr;
        payload_len = 0;
        if (!msg || msg->Size() < 2) {
            return false;
        }
        const auto* base = (const uint8_t*)msg->DataAddr();
        const size_t n = (size_t)msg->Size();
        size_t i = 0;
        auto read_varint = [&](uint64_t& out) -> bool {
            out = 0;
            int shift = 0;
            while (i < n && shift < 64) {
                uint8_t b = base[i++];
                out |= (uint64_t)(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    return true;
                }
                shift += 7;
            }
            return false;
        };
        // pass 1: 外层 tc.Message,记录 type(field 10) 和 audio_frame(field 80) 子消息位置
        bool is_audio = false;
        const uint8_t* sub = nullptr;
        size_t sub_len = 0;
        while (i < n) {
            uint64_t tag = 0;
            if (!read_varint(tag)) {
                return false;
            }
            const uint32_t field = (uint32_t)(tag >> 3);
            const uint32_t wire = (uint32_t)(tag & 0x7);
            if (field == 10 && wire == 0) {
                uint64_t type = 0;
                if (!read_varint(type)) {
                    return false;
                }
                is_audio = (type == 40); // tc_message.proto: kAudioFrame = 40
                continue;
            }
            if (field == 80 && wire == 2) {
                uint64_t len = 0;
                if (!read_varint(len) || i + (size_t)len > n) {
                    return false;
                }
                sub = base + i;
                sub_len = (size_t)len;
                i += (size_t)len;
                continue;
            }
            switch (wire) {
                case 0: { uint64_t v; if (!read_varint(v)) { return false; } break; }
                case 1: i += 8; break;
                case 2: {
                    uint64_t len = 0;
                    if (!read_varint(len)) { return false; }
                    i += (size_t)len;
                    break;
                }
                case 5: i += 4; break;
                default: return false; // group 等不支持
            }
            if (i > n) {
                return false;
            }
        }
        if (!is_audio || !sub) {
            return false;
        }
        // pass 2: AudioFrame 子消息内找 data(field 5, bytes)
        const uint8_t* p = sub;
        const uint8_t* end = sub + sub_len;
        while (p < end) {
            auto rv = [&](const uint8_t*& cur, uint64_t& out) -> bool {
                out = 0;
                int shift = 0;
                while (cur < end && shift < 64) {
                    uint8_t b = *cur++;
                    out |= (uint64_t)(b & 0x7F) << shift;
                    if (!(b & 0x80)) {
                        return true;
                    }
                    shift += 7;
                }
                return false;
            };
            uint64_t tag = 0;
            if (!rv(p, tag)) {
                return false;
            }
            const uint32_t field = (uint32_t)(tag >> 3);
            const uint32_t wire = (uint32_t)(tag & 0x7);
            if (field == 5 && wire == 2) {
                uint64_t len = 0;
                if (!rv(p, len) || (size_t)(end - p) < (size_t)len || len == 0) {
                    return false;
                }
                payload = (const char*)p;
                payload_len = (size_t)len;
                return true;
            }
            switch (wire) {
                case 0: { uint64_t v; if (!rv(p, v)) { return false; } break; }
                case 1: p += 8; break;
                case 2: {
                    uint64_t len = 0;
                    if (!rv(p, len)) { return false; }
                    p += (size_t)len;
                    break;
                }
                case 5: p += 4; break;
                default: return false;
            }
            if (p > end) {
                return false;
            }
        }
        return false;
    }

    void UdpPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        // 只关心 kAudioFrame:提取 Opus payload 打成 UDP 音频包广播给绑定会话;
        // 其它 proto(控制类)仍走 ws 通道,这里直接忽略
        const char* payload = nullptr;
        size_t payload_len = 0;
        if (!ExtractAudioPayload(msg, payload, payload_len)) {
            return;
        }
        if (!HasBoundSession()) {
            return;
        }
        // 与视频同一时钟源:steady_clock 单调毫秒
        auto ts = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() & 0xffffffff);
        auto pkt = GrUdpProtocol::BuildAudioPacket(audio_seq_++, ts, payload, payload_len);
        if (!pkt) {
            return;
        }
        int64_t total_sent = 0;
        sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
            if (!us->bound_ || !us->sess_) {
                return;
            }
            total_sent += pkt->Size();
            // pkt 捕获进回调保活,直到 asio 拷进发件缓冲
            us->sess_->async_send(pkt->CStr(), pkt->Size(), [pkt](std::size_t) {});
        });
        if (total_sent > 0) {
            ReportSentDataSize((int)total_sent);
        }
    }

    bool UdpPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        // 空实现:同上
        return false;
    }

    void UdpPlugin::StartInternal() {
        auto fn_conn_id = [](std::shared_ptr<asio2::udp_session> &sess_ptr) -> std::string {
            return sess_ptr->remote_address() + ":" + std::to_string(sess_ptr->remote_port());
        };

        server_ = std::make_shared<asio2::udp_server>();
        server_->bind_recv([=, this](std::shared_ptr<asio2::udp_session>& session_ptr, std::string_view data) {
            auto opt_sess = sessions_.TryGet(fn_conn_id(session_ptr));
            if (!opt_sess.has_value()) {
                // bind_connect 正常先于首包到达,拿不到说明时序异常,直接丢
                return;
            }
            opt_sess.value()->last_seen_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            // ParseCommon 分流:只处理控制包(上行视频/音频 P2 才启用)
            if (GrUdpProtocol::ParseCommon(data.data(), data.size()) == GrUdpProtocol::kPktCtrl) {
                HandleCtrlPacket(opt_sess.value(), data.data(), data.size());
            }

        }).bind_connect([=, this](std::shared_ptr<asio2::udp_session>& session_ptr) {
            auto conn_id = fn_conn_id(session_ptr);
            auto udp_sess = std::make_shared<UdpSession>();
            udp_sess->conn_id_ = conn_id;
            udp_sess->sess_ = session_ptr;
            udp_sess->last_seen_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            sessions_.Insert(conn_id, udp_sess);
            LOGI("udp client enter : {} {} ; {} {}",
                   session_ptr->remote_address().c_str(), session_ptr->remote_port(),
                   session_ptr->local_address().c_str(), session_ptr->local_port());

        }).bind_disconnect([=, this](auto &session_ptr) {
            auto conn_id = fn_conn_id(session_ptr);
            std::shared_ptr<UdpSession> removed;
            {
                std::lock_guard<std::mutex> lk(bind_mtx_);
                auto opt_sess = sessions_.RemoveIf(conn_id, [&](const std::shared_ptr<UdpSession>& cur) {
                    return cur && cur->sess_ == session_ptr;
                });
                if (opt_sess.has_value()) {
                    removed = opt_sess.value();
                    if (removed->bound_.exchange(false)) {
                        bound_count_--;
                    }
                    else {
                        removed.reset(); // 未绑定会话不算媒体客户端,不发断开事件
                    }
                }
            }
            if (!removed) {
                // endpoint 字符串被新连接复用,或该会话已被 Sweep 摘除;
                // 这里不能误删新会话,只当作迟到/重复的旧断开事件。
                LOGI("udp stale disconnect ignored: {}", conn_id);
                return;
            }
            if (removed) {
                NotifyMediaClientDisConnected(removed->conn_id_, removed->stream_id_,
                                              removed->device_id_, removed->begin_timestamp_);
            }
            LOGI("udp client leave : {} {} {}",
                   session_ptr->remote_address().c_str(), session_ptr->remote_port(),
                   asio2::last_error_msg().c_str());
        }).bind_start([&]() {
            if (asio2::get_last_error()) {
                LOGE("start udp server failure : {} {}",
                       asio2::last_error_val(), asio2::last_error_msg().c_str());
            }
            else {
                LOGI("start udp server success : {} {}",
                       server_->listen_address().c_str(), server_->listen_port());
                // 一帧 ~89 个包(~125KB)毫秒内突发下发,默认发送缓冲易满;
                // 发送缓冲调 4MB、接收 1MB,读回值打出来(Windows 上可能与设置值不同)
                asio::error_code ec;
                auto& sock = server_->acceptor();
                sock.set_option(asio::socket_base::send_buffer_size(4 * 1024 * 1024), ec);
                if (ec) LOGW("udp server set sndbuf 4MB failed: {}", ec.message());
                sock.set_option(asio::socket_base::receive_buffer_size(1 * 1024 * 1024), ec);
                if (ec) LOGW("udp server set rcvbuf 1MB failed: {}", ec.message());
                asio::socket_base::send_buffer_size snd;
                asio::socket_base::receive_buffer_size rcv;
                sock.get_option(snd, ec);
                sock.get_option(rcv, ec);
                LOGI("udp server socket buffer: snd = {}, rcv = {}", snd.value(), rcv.value());
            }
        }).bind_stop([&]() {
            LOGI("stop udp server : {} {}",
                   asio2::last_error_val(), asio2::last_error_msg().c_str());
        }).bind_init([&]() {

        });

        // 裸 UDP(不再 use_kcp):视频重传是负优化,丢了靠客户端报 IDR 恢复
        server_->start("0.0.0.0", udp_listen_port_);
    }

    void UdpPlugin::HandleCtrlPacket(const std::shared_ptr<UdpSession>& udp_sess, const char* data, size_t size) {
        // kCtrlFrameStatus 是定长二进制体,ParseCtrl 不解析,走专门解析
        uint32_t fs_frame = 0;
        uint16_t fs_received = 0, fs_lost = 0;
        if (GrUdpProtocol::ParseFrameStatus(data, size, fs_frame, fs_received, fs_lost)) {
            HandleFrameStatus(fs_frame, fs_received, fs_lost);
            return;
        }
        std::string s1, s2;
        auto subtype = GrUdpProtocol::ParseCtrl(data, size, s1, s2);
        switch (subtype) {
            case GrUdpProtocol::kCtrlHello:
                HandleHello(udp_sess, s1 /*device_id*/, s2 /*stream_id*/);
                break;
            case GrUdpProtocol::kCtrlHeartbeat:
                HandleHeartbeat(udp_sess, s1 /*stream_id*/);
                break;
            case GrUdpProtocol::kCtrlIdrRequest: {
                // 客户端组帧判丢后请求补 IDR;s1 为 mon_name(空 = 全屏)。
                // 判丢帧与 IDR 请求 1:1,据此累计窗口判丢帧数(见 udp_plugin.h 注释)
                stat_lost_frames_++;
                auto event = std::make_shared<GrPluginInsertIdrEvent>();
                event->mon_name_ = s1;
                this->CallbackEvent(event);
                break;
            }
            case GrUdpProtocol::kCtrlIdrKeepalive: {
                // 连接初始化/无帧超时补关键帧:行为同 IDR 请求,但不计入丢帧统计,
                // 否则客户端刚连上自动补几发 IDR 就会把动态 FEC 刷到上限。
                auto event = std::make_shared<GrPluginInsertIdrEvent>();
                event->mon_name_ = s1;
                this->CallbackEvent(event);
                break;
            }
            case GrUdpProtocol::kCtrlRfi: {
                // s1 = invalid_frame_index(字符串),s2 = mon_name(空=全屏)。
                // 丢整帧后优先走参考帧失效,不插 IDR;不支持 RFI 的编码器由上层忽略,
                // 客户端会在 2s 无完整帧后回退 IDR keepalive。
                auto event = std::make_shared<GrPluginInvalidateRefFrameEvent>();
                try {
                    event->invalid_frame_index_ = std::stoull(s1);
                } catch (...) {
                    event->invalid_frame_index_ = 0;
                }
                event->mon_name_ = s2;
                LOGI("udp rfi request: invalid_frame={}, mon={}", event->invalid_frame_index_, event->mon_name_);
                rfi_pending_ = true;
                this->CallbackEvent(event);
                break;
            }
            default:
                break;
        }
    }

    void UdpPlugin::HandleFrameStatus(uint32_t frame_index, uint16_t received, uint16_t lost) {
        (void)frame_index;
        (void)received;
        stat_complete_frames_++;
        stat_recovered_shards_ += lost;
    }

    void UdpPlugin::AdjustFecWindow() {
        int complete = stat_complete_frames_.exchange(0);
        int lost = stat_lost_frames_.exchange(0);
        int recovered = stat_recovered_shards_.exchange(0);
        uint64_t sent_shards = stat_sent_shards_.exchange(0);
        uint64_t short_writes = stat_send_short_writes_.exchange(0);
        int total = complete + lost;
        if (total <= 0) {
            return; // 窗口内无媒体流量,不调整不刷日志
        }
        double loss_rate = (double)lost / (double)total;
        int cur = fec_percent_.load();
        if (lost > 0 && cur < kFecMaxPercent) {
            fec_percent_ = std::min(kFecMaxPercent, cur + 10);
            LOGW("udp fec window: loss {:.1f}% ({}/{} frames), recovered {} shards, raise fec {}% -> {}%",
                 loss_rate * 100.0, lost, total, recovered, cur, fec_percent_.load());
        }
        else if (lost == 0 && recovered == 0 && cur > configured_fec_percent_) {
            fec_percent_ = std::max(configured_fec_percent_, cur - 5);
            LOGI("udp fec window: loss {:.1f}% ({}/{} frames), recovered {} shards, lower fec {}% -> {}%",
                 loss_rate * 100.0, lost, total, recovered, cur, fec_percent_.load());
        }
        else {
            LOGI("udp fec window: frames {} (lost {}, {:.1f}%), recovered {} shards, sent {} shards, short_writes {}, fec {}%",
                 total, lost, loss_rate * 100.0, recovered, sent_shards, short_writes, cur);
        }
    }

    bool UdpPlugin::HasBoundSession() {
        bool has_bound = false;
        sessions_.ApplyAll([&](const std::string&, const std::shared_ptr<UdpSession>& us) {
            if (us && us->bound_ && us->sess_) {
                has_bound = true;
            }
        });
        return has_bound;
    }

    void UdpPlugin::HandleHello(const std::shared_ptr<UdpSession>& udp_sess,
                                const std::string& device_id, const std::string& stream_id) {
        if (udp_sess->kicked_) {
            LOGW("kicked udp endpoint tries to hello again, ignore: {}", udp_sess->conn_id_);
            return;
        }
        if (device_id.empty() || stream_id.empty()) {
            LOGW("kCtrlHello with empty device_id/stream_id, drop. from: {}", udp_sess->conn_id_);
            return;
        }
        auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
        std::shared_ptr<UdpSession> old_bound;
        {
            std::lock_guard<std::mutex> lk(bind_mtx_);
            if (udp_sess->bound_ && udp_sess->stream_id_ == stream_id) {
                // 同会话重复 Hello:刷新心跳即可
                udp_sess->last_heartbeat_ms_ = now;
                return;
            }
            // 互踢:UDP 与 RTC local 保持一致,任意已绑定会话都算占用;
            // 新 Hello 顶掉旧绑定会话,而不是只在同 stream_id 内互踢。
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (us != udp_sess && us->bound_) {
                    old_bound = us;
                }
            });
            if (old_bound) {
                old_bound->bound_ = false;
                old_bound->kicked_ = true;
                bound_count_--;
                LOGW("stream {} taken over, kick old session: {} (old stream: {})", stream_id, old_bound->conn_id_, old_bound->stream_id_);
            }

            udp_sess->device_id_ = device_id;
            udp_sess->stream_id_ = stream_id;
            udp_sess->begin_timestamp_ = now;
            udp_sess->last_heartbeat_ms_ = now;
            udp_sess->bound_ = true;
            bound_count_++;
        }
        if (old_bound && old_bound->sess_) {
            // 通知旧客户端"被接管",再补一条断开事件让统计/状态机闭环
            auto kick = GrUdpProtocol::BuildKick("taken over");
            old_bound->sess_->async_send(kick->CStr(), kick->Size(), [kick](std::size_t) {});
            NotifyMediaClientDisConnected(old_bound->conn_id_, old_bound->stream_id_,
                                          old_bound->device_id_, old_bound->begin_timestamp_);
        }
        // 绑定成功:路由器收到连接事件会自动触发全屏 IDR,不用再单独请求
        NotifyMediaClientConnected(udp_sess->conn_id_, stream_id, device_id, now);
        LOGI("udp media session bound: {} => device: {}, stream: {}", udp_sess->conn_id_, device_id, stream_id);
    }

    void UdpPlugin::HandleHeartbeat(const std::shared_ptr<UdpSession>& udp_sess, const std::string& stream_id) {
        // 被新连接互踢的旧 endpoint 不得再凭 stream_id 抢回绑定
        if (udp_sess->kicked_) {
            return;
        }
        auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
        if (udp_sess->bound_ && udp_sess->stream_id_ == stream_id) {
            udp_sess->last_heartbeat_ms_ = now;
            return;
        }
        // 当前会话未绑定但 stream_id 匹配已绑定会话:NAT 换端口重建了底层会话,
        // 把绑定迁到当前会话(同一逻辑客户端,不发连接/断开事件)
        std::lock_guard<std::mutex> lk(bind_mtx_);
        std::shared_ptr<UdpSession> bound_sess;
        sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
            if (us != udp_sess && us->bound_ && us->stream_id_ == stream_id) {
                bound_sess = us;
            }
        });
        if (!bound_sess) {
            return;
        }
        LOGI("udp session rebound by heartbeat: {} => {} (stream: {})",
             bound_sess->conn_id_, udp_sess->conn_id_, stream_id);
        udp_sess->device_id_ = bound_sess->device_id_;
        udp_sess->stream_id_ = stream_id;
        udp_sess->begin_timestamp_ = bound_sess->begin_timestamp_;
        udp_sess->last_heartbeat_ms_ = now;
        udp_sess->bound_ = true;
        bound_sess->bound_ = false;
    }

    void UdpPlugin::SweepDeadSessions() {
        auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
        std::vector<std::shared_ptr<UdpSession>> dead_sessions;
        std::vector<std::shared_ptr<UdpSession>> stale_sessions;
        {
            std::lock_guard<std::mutex> lk(bind_mtx_);
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (us->kicked_ || (!us->bound_ && now - us->last_seen_ms_.load() > kUnboundSessionTimeoutMs)) {
                    // 被踢/从未绑定且已无流量:直接摘除并停止底层会话,不发断开事件
                    stale_sessions.push_back(us);
                }
                else if (us->bound_ && now - us->last_heartbeat_ms_.load() > kHeartbeatTimeoutMs) {
                    us->bound_ = false;
                    bound_count_--;
                    dead_sessions.push_back(us);
                }
            });
        }
        for (const auto& us : dead_sessions) {
            LOGW("udp media session heartbeat timeout: {} (stream: {})", us->conn_id_, us->stream_id_);
            NotifyMediaClientDisConnected(us->conn_id_, us->stream_id_,
                                          us->device_id_, us->begin_timestamp_);
            // 摘掉会话并停掉底层 session(bind_disconnect 再进来时 bound_ 已是 false,不会重复发事件)
            sessions_.RemoveIf(us->conn_id_, [&](const std::shared_ptr<UdpSession>& cur) {
                return cur == us;
            });
            if (us->sess_) {
                us->sess_->stop();
            }
        }
        for (const auto& us : stale_sessions) {
            LOGW("udp stale session swept: {} (stream: {}, kicked: {})", us->conn_id_, us->stream_id_, us->kicked_.load());
            sessions_.RemoveIf(us->conn_id_, [&](const std::shared_ptr<UdpSession>& cur) {
                return cur == us;
            });
            if (us->sess_) {
                us->sess_->stop();
            }
        }
    }

    uint8_t UdpPlugin::MonSlotOf(const std::string& mon_name) {
        std::lock_guard<std::mutex> lk(mon_slot_mtx_);
        auto it = mon_slots_.find(mon_name);
        if (it != mon_slots_.end()) {
            return it->second;
        }
        auto slot = next_mon_slot_++;
        mon_slots_[mon_name] = slot;
        LOGI("udp mon slot assigned: {} => {}", mon_name, (int)slot);
        return slot;
    }

    // data: encode video frame, h264/h265/...
    void UdpPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                                        const GrPluginEncodedVideoType& video_type,
                                        const std::shared_ptr<Data>& data,
                                        uint64_t frame_index,
                                        int frame_width,
                                        int frame_height,
                                        bool key) {
        if (!data || data->Size() <= 0 || !HasBoundSession()) {
            return;
        }
        static std::atomic_uint64_t s_udp_enc_frames{0};
        auto enc_n = ++s_udp_enc_frames;
        if (enc_n == 1 || enc_n % 300 == 0) {
            LOGI("udp OnEncodedVideoFrame #{}, bound_count={}, sessions={}, frame_index={}, key={}, bytes={}",
                 enc_n, bound_count_.load(), sessions_.Size(), frame_index, key, data->Size());
        }
        uint8_t codec;
        if (video_type == GrPluginEncodedVideoType::kH264) {
            codec = GrUdpProtocol::kCodecH264;
        }
        else if (video_type == GrPluginEncodedVideoType::kH265) {
            codec = GrUdpProtocol::kCodecH265;
        }
        else {
            return; // 其它编码类型不在 UDP 媒体面范围内
        }

        GrUdpProtocol::VideoFrameMeta meta;
        // 透传编码器 frame_index。RFI 恢复依赖客户端上报的 frame_index 与 NVENC
        // inputTimeStamp 完全一致;回退/重连场景已由 client 侧 SOF+key 重流识别处理。
        meta.frame_index_ = (uint32_t)(frame_index & 0xffffffff);
        // steady_clock 单调时钟,客户端按它算帧间间隔/延迟,不受系统时间跳变影响
        meta.timestamp_ms_ = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() & 0xffffffff);
        meta.key_ = key;
        meta.codec_ = codec;
        meta.frame_width_ = (uint16_t)frame_width;
        meta.frame_height_ = (uint16_t)frame_height;
        meta.mon_slot_ = MonSlotOf(mon_name);
        meta.mon_name_ = mon_name;
        meta.rfi_recover_ = rfi_pending_.exchange(false);

        auto shards = GrUdpProtocol::ShardVideoFrame(meta, data->CStr(), (size_t)data->Size(),
                                                     udp_mtu_, fec_percent_);
        if (shards.empty()) {
            return;
        }

        int64_t total_sent = 0;
        // 帧内 pacing:高动态大帧一帧上百个包,背靠背突发会打爆
        // 客户端 socket 缓冲 → 丢包 → 请 IDR → 巨型 IDR 再丢的死循环。
        // 外层按 shard chunk 摊开,内层遍历 bound 会话;每批发完睡 1ms(最后一批不睡)。
        const size_t total_pkts = shards.size();
        for (size_t chunk_base = 0; chunk_base < total_pkts; chunk_base += kPaceChunkSize) {
            const size_t chunk_end = std::min(chunk_base + kPaceChunkSize, total_pkts);
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (!us->bound_ || !us->sess_) {
                    return;
                }
                for (size_t i = chunk_base; i < chunk_end; i++) {
                    const auto& shard = shards[i];
                    total_sent += shard->Size();
                    // shard 捕获进回调保活,直到 asio 拷进发件缓冲
                    stat_sent_shards_++;
                    us->sess_->async_send(shard->CStr(), shard->Size(), [shard, this](std::size_t bytes_sent) {
                        if (bytes_sent != shard->Size()) {
                            stat_send_short_writes_++;
                        }
                    });
                }
            });
            if (chunk_end < total_pkts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kPaceSleepMs));
            }
        }
        if (total_sent > 0) {
            ReportSentDataSize((int)total_sent);
        }
    }

    void UdpPlugin::NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp) {
        auto event = std::make_shared<GrPluginClientConnectedEvent>();
        event->conn_id_ = conn_id;
        event->stream_id_ = stream_id;
        event->conn_type_ = "UDP";
        event->visitor_device_id_ = visitor_device_id;
        event->begin_timestamp_ = begin_timestamp;
        this->CallbackEvent(event);
        LOGI("Conn id: {}, visitor device id: {}", stream_id, visitor_device_id);
    }

    void UdpPlugin::NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp) {
        auto event = std::make_shared<GrPluginClientDisConnectedEvent>();
        event->conn_id_ = conn_id;
        event->stream_id_ = stream_id;
        event->visitor_device_id_ = visitor_device_id;
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->duration_ = event->end_timestamp_ - begin_timestamp;
        this->CallbackEvent(event);
        LOGI("DisConn id: {}, visitor device id: {}, duration: {}", stream_id, visitor_device_id, event->duration_);
    }

    int UdpPlugin::GetConnectedClientsCount() {
        return bound_count_;
    }

    bool UdpPlugin::IsOnlyAudioClients() {
        return false;
    }

    bool UdpPlugin::IsWorking() {
        return GetConnectedClientsCount() > 0;
    }

    bool UdpPlugin::HasEnoughBufferForQueuingMediaMessages() {
        return true;
    }

    bool UdpPlugin::HasEnoughBufferForQueuingFtMessages() {
        return true;
    }

}
