//
// Created RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面,见 udp_transport.h 头注释
//

#include "udp_transport.h"
#include <chrono>
#include <algorithm>
#include <thread>
#include <unordered_map>
#include "px_render/modules/module_ids.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/async_delay.h"
#include "px_common_new/async_runtime.h"
#include "px_common_new/asio_client_shutdown.h"
#include "px_common_new/async_scope_drain.h"
#include "px_common_new/time_util.h"
#include "px_common_new/px_udp_protocol.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"

namespace px
{
    namespace {
        constexpr auto kHeartbeatScanInterval = std::chrono::seconds(2);
        constexpr auto kFecWindow = std::chrono::seconds(5);
        constexpr auto kControlScopeDrainTimeout = std::chrono::seconds(5);
    }

    void UdpWinHandleCloser::operator()(void* handle) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 HANDLE boundary
        if (handle) {
            CloseHandle(handle);
        }
    }

    class UdpRuntimeState final
        : public std::enable_shared_from_this<UdpRuntimeState> {
    public:
        UdpRuntimeState(std::shared_ptr<PxAsyncRuntime> async_runtime,
                        CompatibilityEventCallback event_dispatcher,
                        int fec_percent)
            : fec_percent_(fec_percent),
              event_dispatcher_(std::move(event_dispatcher)),
              configured_fec_percent_(fec_percent),
              control_scope_(PxAsyncScope::Create(std::move(async_runtime), PxAsyncLane::kControl)) {
        }

        [[nodiscard]] bool Start(int listen_port);
        void Stop();
        [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(
            const std::shared_ptr<UdpRuntimeState>& owner,
            std::chrono::steady_clock::time_point deadline);
        [[nodiscard]] bool IsQuiescent() const;
        void HandleCtrlPacket(
            const std::shared_ptr<UdpSession>& udp_session,
            const char* data, size_t size); // NOLINT(gammaray-raw-pointer-boundary) Synchronous UDP byte-view boundary
        void HandleHello(
            const std::shared_ptr<UdpSession>& udp_session,
            const std::string& association_code, const std::string& stream_id);
        void HandleHeartbeat(
            const std::shared_ptr<UdpSession>& udp_session,
            const std::string& association_code);
        void HandleFrameStatus(
            uint32_t frame_index, uint16_t received, uint16_t lost);
        void AdjustFecWindow();
        bool HasBoundSession();
        void SweepDeadSessions();
        void UpdateMediaAssociation(const UdpMediaAssociation& association);

        std::shared_ptr<asio2::udp_server> server_;
        ConcurrentHashMap<std::string, std::shared_ptr<UdpSession>> sessions_;
        std::mutex bind_mutex_;
        struct PendingMediaAssociation {
            std::string logical_session_id_;
            std::string stream_id_;
            int64_t expires_at_ms_ = 0;
            std::string endpoint_id_;
            bool force_gdi_ = false;
        };
        std::unordered_map<std::string, PendingMediaAssociation> media_associations_;
        // UDP is a media-only transport. One render instance has at most one
        // active UDP media endpoint, while WS remains the owner of the logical
        // session and may revoke the endpoint at any time.
        std::string active_media_association_code_;
        std::atomic_int bound_count_{0};
        std::atomic_int fec_percent_{20};
        std::atomic_int stat_complete_frames_{0};
        std::atomic_int stat_lost_frames_{0};
        std::atomic_int stat_recovered_shards_{0};
        std::atomic_uint64_t stat_sent_shards_{0};
        std::atomic_uint64_t stat_send_short_writes_{0};
        std::atomic_bool rfi_pending_{false};

    private:
        static PxAwaitable<void> RunHeartbeatSweepLoop(std::weak_ptr<UdpRuntimeState> weak_runtime);
        static PxAwaitable<void> RunFecWindowLoop(std::weak_ptr<UdpRuntimeState> weak_runtime);
        std::shared_ptr<PxAsyncScope> BeginStop();
        void FinishStop();

        CompatibilityEventCallback event_dispatcher_;
        int configured_fec_percent_ = 20;
        std::shared_ptr<PxAsyncScope> control_scope_{};
        std::atomic_bool stopping_{false};
        static constexpr int64_t kHeartbeatTimeoutMs = 10000;
        static constexpr int64_t kUnboundSessionTimeoutMs = 10000;
        static constexpr int kFecMaxPercent = 60;
    };

    UdpTransport::UdpTransport(std::shared_ptr<PxAsyncRuntime> async_runtime)
        : async_runtime_(std::move(async_runtime)) {
    }

    std::string UdpTransport::Id() const {
        return kNetUdpTransportId;
    }

    std::string UdpTransport::Name() const {
        return "Net UDP";
    }

    std::string UdpTransport::VersionName() const {
        return "1.2.0";
    }

    uint32_t UdpTransport::VersionCode() const {
        return 120;
    }

    std::string UdpTransport::Description() const {
        return "Network via UDP";
    }

    bool UdpTransport::Start(
        const RenderModuleConfiguration& configuration) {
        if (!RenderModule::Start(configuration)) {
            return false;
        }
        const int fec_percent = configuration.udp_fec_percent;
        udp_listen_port_ = static_cast<int>(configuration.udp_listen_port);
        if (configuration.udp_mtu >= 576 && configuration.udp_mtu <= 1500) {
            udp_mtu_ = configuration.udp_mtu;
        }
        if (!async_runtime_ || async_runtime_->IsStopping()) {
            LOGE("event=module.start component=net_udp code=ASYNC_RUNTIME_UNAVAILABLE "
                 "operation=start_control_workflows outcome=failed recoverable=false");
            RenderModule::Stop();
            return false;
        }
        runtime_ = std::make_shared<UdpRuntimeState>(async_runtime_, MakeImmediateCompatibilityEventDispatcher(), fec_percent);
        // Windows sleep 默认 15.6ms 粒度,先把计时器分辨率提到 1ms(高精度 waitable timer 不受此限)
        timer_resolution_active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        // Sunshine 同款高精度 pacing 定时器(Win10 1809+;失败退回普通 waitable timer)
        pace_timer_.reset(CreateWaitableTimerEx(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS));
        if (!pace_timer_) {
            pace_timer_.reset(CreateWaitableTimerEx(
                nullptr, nullptr, 0, TIMER_ALL_ACCESS));
        }
        LOGI("Listen port: {}, fec percent: {}, mtu: {}, pacing: {}Mbps rate-limited (sunshine), timer={}",
             udp_listen_port_, runtime_->fec_percent_.load(), udp_mtu_, kRateControlBitsPerSec / 1000000,
             pace_timer_ ? "ok" : "none");
        if (!runtime_->Start(udp_listen_port_)) {
            runtime_.reset();
            ReleasePacingResources();
            RenderModule::Stop();
            return false;
        }
        return true;
    }

    bool UdpTransport::Destroy() {
        RenderModule::Stop();
        if (runtime_) {
            runtime_->Stop();
            if (runtime_->IsQuiescent()) {
                runtime_.reset();
            }
        }
        ReleasePacingResources();
        return RenderModule::Destroy();
    }

    PxAwaitable<PxResult<void>> UdpTransport::StopAsync(
        const std::shared_ptr<UdpTransport>& owner,
        const std::chrono::steady_clock::time_point deadline) {
        if (!owner) {
            co_return PxResult<void>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "net-udp.stop", "UDP transport owner is missing"));
        }
        owner->RenderModule::Stop();
        const auto runtime = owner->runtime_;
        if (runtime) {
            const auto stopped = co_await UdpRuntimeState::StopAsync(runtime, deadline);
            if (!stopped) {
                co_return stopped;
            }
        }
        owner->runtime_.reset();
        owner->ReleasePacingResources();
        co_return PxResult<void>::Success();
    }

    void UdpTransport::ReleasePacingResources() {
        pace_timer_.reset();
        if (timer_resolution_active_) {
            timeEndPeriod(1);
            timer_resolution_active_ = false;
        }
    }

    void UdpTransport::UpdateUdpMediaAssociation(
        const UdpMediaAssociation& association) {
        if (!runtime_) {
            return;
        }
        runtime_->UpdateMediaAssociation(association);
    }

    // wire 级扫描 px.Message,提取 kAudioFrame(40) 里 AudioFrame.data(field 5, bytes)
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
        // pass 1: 外层 px.Message,记录 type(field 10) 和 audio_frame(field 80) 子消息位置
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
                is_audio = (type == 40); // px_message.proto: kAudioFrame = 40
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

    void UdpTransport::Broadcast(std::shared_ptr<Data> msg, bool run_through) {
        // 只关心 kAudioFrame:提取 Opus payload 打成 UDP 音频包广播给绑定会话;
        // 其它 proto(控制类)仍走 ws 通道,这里直接忽略
        const char* payload = nullptr;
        size_t payload_len = 0;
        if (!ExtractAudioPayload(msg, payload, payload_len)) {
            return;
        }
        const auto runtime = runtime_;
        if (!runtime || !runtime->HasBoundSession()) {
            return;
        }
        // 与视频同一时钟源:steady_clock 单调毫秒
        auto ts = (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() & 0xffffffff);
        auto pkt = PxUdpProtocol::BuildAudioPacket(audio_seq_++, ts, payload, payload_len);
        if (!pkt) {
            return;
        }
        int64_t total_sent = 0;
        runtime->sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
            if (!us->bound_ || !us->sess_) {
                return;
            }
            total_sent += pkt->Size();
            // pkt 捕获进回调保活,直到 asio 拷进发件缓冲
            us->sess_->async_send(pkt->CStr(), pkt->Size(), [pkt](std::size_t) {});
        });
        if (total_sent > 0) {
            ReportDataSent(total_sent);
        }
    }

    bool UdpTransport::SendToStream(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        // 空实现:同上
        return false;
    }

    bool UdpRuntimeState::Start(int listen_port) {
        if (!control_scope_ || !control_scope_->IsAccepting()) {
            LOGE("event=module.start component=net_udp code=ASYNC_SCOPE_CREATE_FAILED "
                 "operation=start_control_workflows outcome=failed recoverable=false");
            return false;
        }
        stopping_ = false;
        const auto connection_id = [](
            const std::shared_ptr<asio2::udp_session>& session) {
            return session->remote_address() + ":"
                + std::to_string(session->remote_port());
        };

        server_ = std::make_shared<asio2::udp_server>();
        const auto weak_runtime = weak_from_this();
        server_->bind_recv([weak_runtime, connection_id](
            std::shared_ptr<asio2::udp_session>& session,
            std::string_view data) {
            const auto runtime = weak_runtime.lock();
            if (!runtime) {
                return;
            }
            auto opt_sess = runtime->sessions_.TryGet(connection_id(session));
            if (!opt_sess.has_value()) {
                // bind_connect 正常先于首包到达,拿不到说明时序异常,直接丢
                return;
            }
            opt_sess.value()->last_seen_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            // ParseCommon 分流:只处理控制包(上行视频/音频 P2 才启用)
            if (PxUdpProtocol::ParseCommon(data.data(), data.size()) == PxUdpProtocol::kPktCtrl) {
                runtime->HandleCtrlPacket(
                    opt_sess.value(), data.data(), data.size());
            }

        }).bind_connect([weak_runtime, connection_id](
            std::shared_ptr<asio2::udp_session>& session) {
            const auto runtime = weak_runtime.lock();
            if (!runtime) {
                return;
            }
            auto conn_id = connection_id(session);
            auto udp_sess = std::make_shared<UdpSession>();
            udp_sess->conn_id_ = conn_id;
            udp_sess->sess_ = session;
            udp_sess->last_seen_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            runtime->sessions_.Insert(conn_id, udp_sess);
            LOGI("udp client enter : {} {} ; {} {}",
                   session->remote_address().c_str(), session->remote_port(),
                   session->local_address().c_str(), session->local_port());

        }).bind_disconnect([weak_runtime, connection_id](auto& session) {
            const auto runtime = weak_runtime.lock();
            if (!runtime) {
                return;
            }
            auto conn_id = connection_id(session);
            std::shared_ptr<UdpSession> removed;
            {
                std::lock_guard lock(runtime->bind_mutex_);
                auto opt_sess = runtime->sessions_.RemoveIf(conn_id, [&](const std::shared_ptr<UdpSession>& cur) {
                    return cur && cur->sess_ == session;
                });
                if (opt_sess.has_value()) {
                    removed = opt_sess.value();
                    if (removed->bound_.exchange(false)) {
                        runtime->bound_count_--;
                        const auto association = runtime->media_associations_.find(
                            removed->association_code_);
                        if (association != runtime->media_associations_.end()
                            && association->second.endpoint_id_ == conn_id) {
                            association->second.endpoint_id_.clear();
                        }
                        if (runtime->active_media_association_code_
                            == removed->association_code_) {
                            runtime->active_media_association_code_.clear();
                        }
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
            LOGI("udp media endpoint disconnected: {} {} {}",
                   session->remote_address().c_str(), session->remote_port(),
                   asio2::last_error_msg().c_str());
        }).bind_start([weak_runtime]() {
            if (asio2::get_last_error()) {
                LOGE("start udp server failure : {} {}",
                       asio2::last_error_val(), asio2::last_error_msg().c_str());
            }
            else if (const auto runtime = weak_runtime.lock()) {
                LOGI("start udp server success : {} {}",
                       runtime->server_->listen_address().c_str(),
                       runtime->server_->listen_port());
                // 一帧 ~89 个包(~125KB)毫秒内突发下发,默认发送缓冲易满;
                // 发送缓冲调 4MB、接收 1MB,读回值打出来(Windows 上可能与设置值不同)
                asio::error_code ec;
                auto& sock = runtime->server_->acceptor();
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
        }).bind_stop([]() {
            LOGI("stop udp server : {} {}",
                   asio2::last_error_val(), asio2::last_error_msg().c_str());
        }).bind_init([]() {

        });

        // 裸 UDP(不再 use_kcp):视频重传是负优化,丢了靠客户端报 IDR 恢复
        const auto started = server_->start("0.0.0.0", listen_port);
        if (!started) {
            LOGE("event=module.start component=net_udp code=UDP_SERVER_START_FAILED "
                 "operation=start_server outcome=failed recoverable=true port={}",
                 listen_port);
            Stop();
            return false;
        }

        const auto heartbeat_started = control_scope_->Spawn("udp-heartbeat-sweep", [weak_runtime]() {
            return RunHeartbeatSweepLoop(weak_runtime);
        });
        const auto fec_started = control_scope_->Spawn("udp-fec-window", [weak_runtime]() {
            return RunFecWindowLoop(weak_runtime);
        });
        if (!heartbeat_started || !fec_started) {
            LOGE("event=module.start component=net_udp code=ASYNC_SCOPE_SPAWN_FAILED "
                 "operation=start_control_workflows outcome=failed recoverable=false heartbeat_started={} fec_started={}",
                 heartbeat_started,
                 fec_started);
            Stop();
            return false;
        }
        return true;
    }

    void UdpRuntimeState::Stop() {
        const auto deadline = std::chrono::steady_clock::now() + kControlScopeDrainTimeout;
        const auto scope = BeginStop();
        if (!scope) {
            FinishStop();
            return;
        }
        if (scope->IsScopeThread()) {
            LOGI("event=async.scope_drain component=net_udp operation=stop_control_workflows outcome=deferred "
                 "reason=shutdown_requested_from_runtime_thread outstanding={}",
                 scope->GetStatistics().outstanding);
            return;
        }
        const auto server = server_;
        const auto adapter_stopped = WaitForAsioObjectStoppedBlocking(server, deadline);
        const auto remaining = std::max(
            std::chrono::milliseconds::zero(),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        if (!adapter_stopped || !scope->WaitFor(remaining)) {
            LOGE("event=async.scope_drain component=net_udp code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                 "operation=stop_control_workflows outcome=timeout recoverable=false outstanding={}",
                 scope->GetStatistics().outstanding);
            return;
        }
        FinishStop();
    }

    PxAwaitable<PxResult<void>> UdpRuntimeState::StopAsync(
        const std::shared_ptr<UdpRuntimeState>& owner,
        const std::chrono::steady_clock::time_point deadline) {
        const auto scope = owner->BeginStop();
        const auto adapter_stopped = co_await WaitForAsioObjectStopped(owner->server_, deadline, "net-udp.adapter-stop");
        if (!adapter_stopped) {
            co_return adapter_stopped;
        }
        if (scope) {
            const auto drained = co_await WaitForAsyncScopeDrain(scope, deadline, "net-udp.stop");
            if (!drained) {
                co_return PxResult<void>::Failure(drained.Error());
            }
        }
        owner->FinishStop();
        co_return PxResult<void>::Success();
    }

    std::shared_ptr<PxAsyncScope> UdpRuntimeState::BeginStop() {
        if (stopping_.exchange(true)) {
            return control_scope_;
        }
        const auto server = server_;
        if (server && !server->is_stopped()) {
            server->post([server] { server->stop(); });
        }
        if (control_scope_) {
            control_scope_->BeginStop();
        }
        return control_scope_;
    }

    void UdpRuntimeState::FinishStop() {
        if ((server_ && !server_->is_stopped()) ||
            (control_scope_ && control_scope_->GetStatistics().outstanding != 0)) {
            return;
        }
        server_.reset();
        sessions_.Clear();
        std::scoped_lock lock(bind_mutex_);
        media_associations_.clear();
        active_media_association_code_.clear();
        control_scope_.reset();
    }

    bool UdpRuntimeState::IsQuiescent() const {
        return (!server_ || server_->is_stopped()) &&
            (!control_scope_ || control_scope_->GetStatistics().outstanding == 0);
    }

    PxAwaitable<void> UdpRuntimeState::RunHeartbeatSweepLoop(std::weak_ptr<UdpRuntimeState> weak_runtime) {
        for (;;) {
            const auto waited = co_await WaitForAsyncDelay(kHeartbeatScanInterval, "udp.heartbeat_sweep.wait");
            if (!waited) {
                co_return;
            }
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime->stopping_) {
                co_return;
            }
            runtime->SweepDeadSessions();
        }
    }

    PxAwaitable<void> UdpRuntimeState::RunFecWindowLoop(std::weak_ptr<UdpRuntimeState> weak_runtime) {
        for (;;) {
            const auto waited = co_await WaitForAsyncDelay(kFecWindow, "udp.fec_window.wait");
            if (!waited) {
                co_return;
            }
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime->stopping_) {
                co_return;
            }
            runtime->AdjustFecWindow();
        }
    }

    void UdpRuntimeState::UpdateMediaAssociation(const UdpMediaAssociation& association) {
        if (association.association_code_.empty()) {
            return;
        }
        std::scoped_lock lock(bind_mutex_);
        if (association.revoke_) {
            const auto existing = media_associations_.find(association.association_code_);
            if (existing != media_associations_.end()) {
                if (!existing->second.endpoint_id_.empty()) {
                    const auto endpoint = sessions_.Remove(existing->second.endpoint_id_);
                    if (endpoint.has_value() && endpoint.value()->bound_.exchange(false)) {
                        --bound_count_;
                    }
                }
                media_associations_.erase(existing);
            }
            if (active_media_association_code_ == association.association_code_) {
                active_media_association_code_.clear();
            }
            LOGI("udp media association revoked: stream={}, remaining={}",
                 association.stream_id_, media_associations_.size());
            return;
        }
        if (association.stream_id_.empty() || association.expires_at_ms_ <= 0) {
            return;
        }
        const auto existing = media_associations_.find(association.association_code_);
        if (existing != media_associations_.end()
            && existing->second.logical_session_id_ == association.logical_session_id_
            && existing->second.stream_id_ == association.stream_id_) {
            // A WS reconnect may refresh the one-time association before the
            // UDP hello arrives. Preserve a successfully bound endpoint.
            existing->second.expires_at_ms_ = association.expires_at_ms_;
            existing->second.force_gdi_ = association.force_gdi_;
            LOGI("udp media association refreshed: stream={}, pending={}",
                 association.stream_id_, media_associations_.size());
            return;
        }
        media_associations_.insert_or_assign(association.association_code_, PendingMediaAssociation{
            .logical_session_id_ = association.logical_session_id_,
            .stream_id_ = association.stream_id_,
            .expires_at_ms_ = association.expires_at_ms_,
            .force_gdi_ = association.force_gdi_,
        });
        LOGI("udp media association registered: stream={}, pending={}",
             association.stream_id_, media_associations_.size());
    }

    void UdpRuntimeState::HandleCtrlPacket(
        const std::shared_ptr<UdpSession>& udp_sess,
        const char* data, // NOLINT(gammaray-raw-pointer-boundary) Synchronous UDP byte-view boundary
        size_t size) {
        // kCtrlFrameStatus 是定长二进制体,ParseCtrl 不解析,走专门解析
        uint32_t fs_frame = 0;
        uint16_t fs_received = 0, fs_lost = 0;
        if (PxUdpProtocol::ParseFrameStatus(data, size, fs_frame, fs_received, fs_lost)) {
            if (udp_sess->bound_) {
                HandleFrameStatus(fs_frame, fs_received, fs_lost);
            }
            return;
        }
        std::string s1, s2;
        auto subtype = PxUdpProtocol::ParseCtrl(data, size, s1, s2);
        switch (subtype) {
            case PxUdpProtocol::kCtrlHello:
                HandleHello(udp_sess, s1 /*association_code*/, s2 /*stream_id*/);
                break;
            case PxUdpProtocol::kCtrlHeartbeat:
                HandleHeartbeat(udp_sess, s1 /*association_code*/);
                break;
            case PxUdpProtocol::kCtrlIdrRequest: {
                if (!udp_sess->bound_) {
                    return;
                }
                // 客户端组帧判丢后请求补 IDR;s1 为 mon_name(空 = 全屏)。
                // 判丢帧与 IDR 请求 1:1,据此累计窗口判丢帧数(见 udp_transport.h 注释)
                stat_lost_frames_++;
                auto event = std::make_shared<PxPluginInsertIdrEvent>();
                event->mon_name_ = s1;
                event_dispatcher_(event);
                break;
            }
            case PxUdpProtocol::kCtrlIdrKeepalive: {
                if (!udp_sess->bound_) {
                    return;
                }
                // 连接初始化/无帧超时补关键帧:行为同 IDR 请求,但不计入丢帧统计,
                // 否则客户端刚连上自动补几发 IDR 就会把动态 FEC 刷到上限。
                auto event = std::make_shared<PxPluginInsertIdrEvent>();
                event->mon_name_ = s1;
                event_dispatcher_(event);
                break;
            }
            case PxUdpProtocol::kCtrlRfi: {
                if (!udp_sess->bound_) {
                    return;
                }
                // s1 = invalid_frame_index(字符串),s2 = mon_name(空=全屏)。
                // 丢整帧后优先走参考帧失效,不插 IDR;不支持 RFI 的编码器由上层忽略,
                // 客户端会在 2s 无完整帧后回退 IDR keepalive。
                auto event = std::make_shared<PxPluginInvalidateRefFrameEvent>();
                try {
                    event->invalid_frame_index_ = std::stoull(s1);
                } catch (...) {
                    event->invalid_frame_index_ = 0;
                }
                event->mon_name_ = s2;
                LOGI("udp rfi request: invalid_frame={}, mon={}", event->invalid_frame_index_, event->mon_name_);
                rfi_pending_ = true;
                event_dispatcher_(event);
                break;
            }
            default:
                break;
        }
    }

    void UdpRuntimeState::HandleFrameStatus(uint32_t frame_index, uint16_t received, uint16_t lost) {
        (void)frame_index;
        (void)received;
        stat_complete_frames_++;
        stat_recovered_shards_ += lost;
    }

    void UdpRuntimeState::AdjustFecWindow() {
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

    bool UdpRuntimeState::HasBoundSession() {
        bool has_bound = false;
        sessions_.ApplyAll([&](const std::string&, const std::shared_ptr<UdpSession>& us) {
            if (us && us->bound_ && us->sess_) {
                has_bound = true;
            }
        });
        return has_bound;
    }

    void UdpRuntimeState::HandleHello(
        const std::shared_ptr<UdpSession>& udp_sess,
        const std::string& association_code, const std::string& stream_id) {
        if (association_code.empty() || stream_id.empty()) {
            LOGW("udp media hello missing association or stream from {}", udp_sess->conn_id_);
            return;
        }
        const auto now = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
        std::shared_ptr<UdpSession> replaced_endpoint;
        bool force_gdi = false;
        {
            std::scoped_lock lock(bind_mutex_);
            const auto association_it = media_associations_.find(association_code);
            if (association_it == media_associations_.end()
                || association_it->second.stream_id_ != stream_id
                // The short expiry gates the first endpoint registration. A
                // bound WS association may subsequently re-register on a NAT
                // port change; WS close remains the authoritative revocation.
                || (association_it->second.expires_at_ms_ <= now
                    && association_it->second.endpoint_id_.empty())) {
                const auto stream_match = std::any_of(
                    media_associations_.begin(), media_associations_.end(),
                    [&stream_id](const auto& item) {
                        return item.second.stream_id_ == stream_id;
                    });
                LOGW("udp media hello has no active WS association from {} (stream={}, pending={}, stream_match={})",
                     udp_sess->conn_id_, stream_id, media_associations_.size(), stream_match);
                return;
            }
            if (!active_media_association_code_.empty()
                && active_media_association_code_ != association_code) {
                LOGW("udp media hello rejected: another endpoint is active");
                return;
            }
            if (udp_sess->bound_ && udp_sess->association_code_ == association_code) {
                udp_sess->last_heartbeat_ms_ = now;
                return;
            }
            if (!association_it->second.endpoint_id_.empty()
                && association_it->second.endpoint_id_ != udp_sess->conn_id_) {
                const auto previous = sessions_.Remove(association_it->second.endpoint_id_);
                if (previous.has_value()) {
                    replaced_endpoint = previous.value();
                    if (replaced_endpoint->bound_.exchange(false)) {
                        --bound_count_;
                    }
                }
            }
            association_it->second.endpoint_id_ = udp_sess->conn_id_;
            active_media_association_code_ = association_code;
            udp_sess->association_code_ = association_code;
            udp_sess->stream_id_ = stream_id;
            force_gdi = association_it->second.force_gdi_;
            udp_sess->begin_timestamp_ = now;
            udp_sess->last_heartbeat_ms_ = now;
            if (!udp_sess->bound_.exchange(true)) {
                ++bound_count_;
            }
        }
        if (replaced_endpoint && replaced_endpoint->sess_) {
            const auto kick = PxUdpProtocol::BuildKick("media endpoint replaced");
            replaced_endpoint->sess_->async_send(kick->CStr(), kick->Size(), [kick](std::size_t) {});
        }
        LOGI("udp media endpoint associated: {} stream={}", udp_sess->conn_id_, stream_id);
        // The capture wake caused by WS open can produce its only initial frame
        // before the UDP hello binds an endpoint. A static desktop would then
        // have no later frame to deliver. Re-run the same capture selection only
        // after binding so the first usable encoded frame cannot fall into that
        // gap.
        auto begin_streaming = std::make_shared<PxPluginReqParamsBeginStreaming>();
        begin_streaming->stream_id_ = stream_id;
        begin_streaming->force_gdi_ = force_gdi;
        event_dispatcher_(begin_streaming);
    }

    void UdpRuntimeState::HandleHeartbeat(
        const std::shared_ptr<UdpSession>& udp_sess,
        const std::string& association_code) {
        if (!udp_sess->bound_ || udp_sess->association_code_ != association_code) {
            return;
        }
        const auto now = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
        std::scoped_lock lock(bind_mutex_);
        const auto association_it = media_associations_.find(association_code);
        if (association_it == media_associations_.end()
            || association_it->second.endpoint_id_ != udp_sess->conn_id_
            || active_media_association_code_ != association_code) {
            return;
        }
        udp_sess->last_heartbeat_ms_ = now;
    }

    void UdpRuntimeState::SweepDeadSessions() {
        auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
        std::vector<std::shared_ptr<UdpSession>> dead_sessions;
        std::vector<std::shared_ptr<UdpSession>> stale_sessions;
        {
            std::lock_guard lock(bind_mutex_);
            sessions_.ApplyAll([&](const std::string& k, const std::shared_ptr<UdpSession>& us) {
                if (us->kicked_ || (!us->bound_ && now - us->last_seen_ms_.load() > kUnboundSessionTimeoutMs)) {
                    // 被踢/从未绑定且已无流量:直接摘除并停止底层会话,不发断开事件
                    stale_sessions.push_back(us);
                }
                else if (us->bound_ && now - us->last_heartbeat_ms_.load() > kHeartbeatTimeoutMs) {
                    us->bound_ = false;
                    bound_count_--;
                    const auto association = media_associations_.find(us->association_code_);
                    if (association != media_associations_.end()
                        && association->second.endpoint_id_ == us->conn_id_) {
                        association->second.endpoint_id_.clear();
                    }
                    if (active_media_association_code_ == us->association_code_) {
                        active_media_association_code_.clear();
                    }
                    dead_sessions.push_back(us);
                }
            });
        }
        for (const auto& us : dead_sessions) {
            LOGW("udp media session heartbeat timeout: {} (stream: {})", us->conn_id_, us->stream_id_);
            // Media endpoint expiry is diagnostic only: it must never announce
            // a logical client disconnect or release a controller lease.
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
        {
            std::scoped_lock lock(bind_mutex_);
            for (auto it = media_associations_.begin(); it != media_associations_.end();) {
                // Expiry protects the first UDP hello. Once the endpoint has
                // been accepted, WS owns revocation and heartbeat maintains
                // liveness; expiring this entry would silently break a healthy
                // media stream after fifteen seconds.
                if (it->second.endpoint_id_.empty() && it->second.expires_at_ms_ <= now) {
                    it = media_associations_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    uint8_t UdpTransport::MonSlotOf(const std::string& mon_name) {
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

    // Sunshine 同款:CreateWaitableTimerEx(HIGH_RESOLUTION) + SetWaitableTimer + WaitForSingleObject,
    // 精确睡到 due 时间点(亚毫秒),而不是 std::this_thread::sleep_for 的粗粒度。
    void UdpTransport::PaceSleep(const std::chrono::steady_clock::duration& duration) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        if (ns <= 0) {
            return;
        }
        if (!pace_timer_) {
            std::this_thread::sleep_for(duration);
            return;
        }
        LARGE_INTEGER due_time;
        due_time.QuadPart = ns / -100;  // 100ns 单位,负数 = 相对时间
        SetWaitableTimer(
            pace_timer_.get(), &due_time, 0, nullptr, nullptr,
            false); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HANDLE boundary
        WaitForSingleObject(
            pace_timer_.get(),
            INFINITE); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HANDLE boundary
    }

    // data: encode video frame, h264/h265/...
    void UdpTransport::SubmitEncodedVideo(const std::string& mon_name,
                                        const PxPluginEncodedVideoType& video_type,
                                        const std::shared_ptr<Data>& data,
                                        uint64_t frame_index,
                                        int frame_width,
                                        int frame_height,
                                        bool key) {
        const auto runtime = runtime_;
        if (!runtime || !data || data->Size() <= 0
            || !runtime->HasBoundSession()) {
            return;
        }
        static std::atomic_uint64_t s_udp_enc_frames{0};
        auto enc_n = ++s_udp_enc_frames;
        if (enc_n == 1 || enc_n % 300 == 0) {
            LOGI("udp OnEncodedVideoFrame #{}, bound_count={}, sessions={}, frame_index={}, key={}, bytes={}",
                 enc_n, runtime->bound_count_.load(), runtime->sessions_.Size(),
                 frame_index, key, data->Size());
        }
        uint8_t codec;
        if (video_type == PxPluginEncodedVideoType::kH264) {
            codec = PxUdpProtocol::kCodecH264;
        }
        else if (video_type == PxPluginEncodedVideoType::kH265) {
            codec = PxUdpProtocol::kCodecH265;
        }
        else {
            return; // 其它编码类型不在 UDP 媒体面范围内
        }

        PxUdpProtocol::VideoFrameMeta meta;
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
        meta.rfi_recover_ = runtime->rfi_pending_.exchange(false);

        auto shards = PxUdpProtocol::ShardVideoFrame(meta, data->CStr(), (size_t)data->Size(),
                                                     udp_mtu_, runtime->fec_percent_);
        if (shards.empty()) {
            return;
        }

        int64_t total_sent = 0;
        // Sunshine 同款 pacing(stream.cpp):按速率上限把一帧的 shard 平滑摊开;
        // 每批发前算精确 due 时间,用高精度 waitable timer 睡到点,跨帧锚定 ratecontrol_next_frame_start。
        // 速率上限按百兆网 80Mbps(而非 Sunshine 的 1Gbps*80%=800Mbps),避免 64KB 级突发打爆路由器缓冲。
        const size_t blocksize = (size_t)udp_mtu_;
        // ratecontrol_packets_in_1ms = 80Mbps/1000/blocksize/8 = 10000/blocksize
        const size_t packets_per_ms = (size_t)(kRateControlBitsPerSec / 1000 / blocksize / 8);
        // Sunshine 单批上限是 64KB,但我们网络是百兆且路由器缓冲小,
        // 64KB 突发会丢包;把单批压到 10 shard(约 14KB),与之前实测最好的 10 shard/1ms 一致。
        const size_t send_batch_size = std::min<size_t>(10, 64 * 1024 / blocksize);

        auto ratecontrol_frame_start = std::max(
            ratecontrol_next_frame_start_, std::chrono::steady_clock::now());
        size_t ratecontrol_frame_packets_sent = 0;
        size_t ratecontrol_group_packets_sent = 0;

        const size_t total_pkts = shards.size();
        size_t next_shard_to_send = 0;
        for (size_t x = 0; x < total_pkts; x++) {
            if (x - next_shard_to_send + 1 >= send_batch_size || x + 1 == total_pkts) {
                if (ratecontrol_group_packets_sent >= packets_per_ms || ratecontrol_frame_packets_sent == 0) {
                    auto due = ratecontrol_frame_start +
                               std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(1)) *
                                 ratecontrol_frame_packets_sent / packets_per_ms;
                    auto now = std::chrono::steady_clock::now();
                    if (now < due) {
                        PaceSleep(due - now);
                    }
                    ratecontrol_group_packets_sent = 0;
                }
                const size_t current_batch_size = x - next_shard_to_send + 1;
                runtime->sessions_.ApplyAll([&](const std::string&, const std::shared_ptr<UdpSession>& us) {
                    if (!us->bound_ || !us->sess_) {
                        return;
                    }
                    for (size_t i = next_shard_to_send; i <= x; i++) {
                        const auto& shard = shards[i];
                        total_sent += shard->Size();
                        // shard 捕获进回调保活,直到 asio 拷进发件缓冲
                        runtime->stat_sent_shards_++;
                        const auto weak_runtime =
                            std::weak_ptr<UdpRuntimeState>(runtime);
                        us->sess_->async_send(
                            shard->CStr(), shard->Size(),
                            [shard, weak_runtime](std::size_t bytes_sent) {
                            if (bytes_sent != shard->Size()) {
                                if (const auto locked = weak_runtime.lock()) {
                                    locked->stat_send_short_writes_++;
                                }
                            }
                        });
                    }
                });
                ratecontrol_group_packets_sent += current_batch_size;
                ratecontrol_frame_packets_sent += current_batch_size;
                next_shard_to_send = x + 1;
            }
        }

        ratecontrol_next_frame_start_ = ratecontrol_frame_start +
                                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(1)) *
                                          ratecontrol_frame_packets_sent / packets_per_ms;

        if (total_sent > 0) {
            ReportDataSent(total_sent);
        }
    }

    int UdpTransport::ConnectedClientCount() const {
        const auto runtime = runtime_;
        return runtime ? runtime->bound_count_.load() : 0;
    }

    bool UdpTransport::HasOnlyAudioClients() const noexcept {
        return false;
    }

    bool UdpTransport::IsWorking() const {
        return ConnectedClientCount() > 0;
    }

    bool UdpTransport::HasMediaCapacity() const noexcept {
        return true;
    }

    bool UdpTransport::HasFileTransferCapacity() const noexcept {
        return true;
    }

}
