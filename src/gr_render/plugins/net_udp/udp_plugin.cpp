//
// Created RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面,见 udp_plugin.h 头注释
//

#include "udp_plugin.h"
#include <chrono>
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
        LOGI("Listen port: {}", udp_listen_port_);
        StartInternal();

        // 心跳扫描:超 10s 无心跳的绑定会话判定掉线
        if (plugin_context_) {
            plugin_context_->StartTimer(kHeartbeatScanIntervalMs, [=, this]() {
                SweepDeadSessions();
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
        return GrNetPlugin::OnDestroy();
    }

    void UdpPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        // 空实现:媒体走 OnEncodedVideoFrame,控制走 ws 通道,UDP 插件不转发 proto
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
            // ParseCommon 分流:只处理控制包(上行视频/音频 P2 才启用)
            if (GrUdpProtocol::ParseCommon(data.data(), data.size()) == GrUdpProtocol::kPktCtrl) {
                HandleCtrlPacket(opt_sess.value(), data.data(), data.size());
            }

        }).bind_connect([=, this](std::shared_ptr<asio2::udp_session>& session_ptr) {
            auto conn_id = fn_conn_id(session_ptr);
            auto udp_sess = std::make_shared<UdpSession>();
            udp_sess->conn_id_ = conn_id;
            udp_sess->sess_ = session_ptr;
            sessions_.Insert(conn_id, udp_sess);
            LOGI("udp client enter : {} {} ; {} {}",
                   session_ptr->remote_address().c_str(), session_ptr->remote_port(),
                   session_ptr->local_address().c_str(), session_ptr->local_port());

        }).bind_disconnect([=, this](auto &session_ptr) {
            auto conn_id = fn_conn_id(session_ptr);
            std::shared_ptr<UdpSession> removed;
            {
                std::lock_guard<std::mutex> lk(bind_mtx_);
                auto opt_sess = sessions_.Remove(conn_id);
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
            if (removed) {
                NotifyMediaClientDisConnected(removed->conn_id_, removed->stream_id_,
                                              removed->device_id_, removed->begin_timestamp_);
            }
            LOGI("udp client leave : {} {} {}",
                   session_ptr->remote_address().c_str(), session_ptr->remote_port(),
                   asio2::last_error_msg().c_str());
        }).bind_start([&]() {
            if (asio2::get_last_error())
                LOGE("start udp server failure : {} {}",
                       asio2::last_error_val(), asio2::last_error_msg().c_str());
            else
                LOGI("start udp server success : {} {}",
                       server_->listen_address().c_str(), server_->listen_port());
        }).bind_stop([&]() {
            LOGI("stop udp server : {} {}",
                   asio2::last_error_val(), asio2::last_error_msg().c_str());
        }).bind_init([&]() {

        });

        // 裸 UDP(不再 use_kcp):视频重传是负优化,丢了靠客户端报 IDR 恢复
        server_->start("0.0.0.0", udp_listen_port_);
    }

    void UdpPlugin::HandleCtrlPacket(const std::shared_ptr<UdpSession>& udp_sess, const char* data, size_t size) {
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
                // 客户端组帧判丢后请求补 IDR;s1 为 mon_name(空 = 全屏)
                auto event = std::make_shared<GrPluginInsertIdrEvent>();
                event->mon_name_ = s1;
                this->CallbackEvent(event);
                break;
            }
            default:
                // kCtrlFrameStatus 等 ParseCtrl 暂不解析的 subtype,忽略
                break;
        }
    }

    void UdpPlugin::HandleHello(const std::shared_ptr<UdpSession>& udp_sess,
                                const std::string& device_id, const std::string& stream_id) {
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
            // 互踢:同 stream_id 已有活跃绑定会话,顶掉旧的
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (us != udp_sess && us->bound_ && us->stream_id_ == stream_id) {
                    old_bound = us;
                }
            });
            if (old_bound) {
                old_bound->bound_ = false;
                bound_count_--;
                LOGW("stream {} taken over, kick old session: {}", stream_id, old_bound->conn_id_);
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
        {
            std::lock_guard<std::mutex> lk(bind_mtx_);
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (us->bound_ && now - us->last_heartbeat_ms_.load() > kHeartbeatTimeoutMs) {
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
            sessions_.Remove(us->conn_id_);
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
        if (!data || data->Size() <= 0 || bound_count_ <= 0) {
            return;
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

        auto shards = GrUdpProtocol::ShardVideoFrame(meta, data->CStr(), (size_t)data->Size());
        if (shards.empty()) {
            return;
        }

        int64_t total_sent = 0;
        sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
            if (!us->bound_ || !us->sess_) {
                return;
            }
            for (const auto& shard : shards) {
                total_sent += shard->Size();
                // shard 捕获进回调保活,直到 asio 拷进发件缓冲
                us->sess_->async_send(shard->CStr(), shard->Size(), [shard](std::size_t) {});
            }
        });
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
