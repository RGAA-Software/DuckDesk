//
// Created RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面,见 udp_transport.h 头注释
//

#include "udp_transport.h"
#include <chrono>
#include <optional>
#include <span>
#include <algorithm>
#include <thread>
#include <unordered_map>
#include <timeapi.h>
#include "px_render/modules/module_ids.h"
#include "px_common/log.h"
#include "px_common/data.h"
#include "px_common/async_delay.h"
#include "px_common/async_runtime.h"
#include "px_common/asio_client_shutdown.h"
#include "px_common/async_scope_drain.h"
#include "px_common/time_util.h"
#include "px_common/px_udp_protocol.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/architecture/runtime/render_execution_context.h"

namespace px {
namespace {
constexpr auto kHeartbeatScanInterval = std::chrono::seconds(2);
constexpr auto kFecWindow = std::chrono::seconds(5);
constexpr auto kControlScopeDrainTimeout = std::chrono::seconds(5);
} // namespace

void UdpWinHandleCloser::operator()(void* handle) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 HANDLE boundary
    if (handle) {
        CloseHandle(handle);
    }
}

class UdpRuntimeState final : public std::enable_shared_from_this<UdpRuntimeState> {
  public:
    UdpRuntimeState(std::shared_ptr<PxAsyncRuntime> async_runtime, RenderEventCallback event_dispatcher, int fec_percent)
        : fec_percent_(fec_percent), event_dispatcher_(std::move(event_dispatcher)), configured_fec_percent_(fec_percent),
          control_scope_(PxAsyncScope::Create(std::move(async_runtime), PxAsyncLane::kControl)) {}

    [[nodiscard]] bool Start(int listen_port);
    void Stop();
    [[nodiscard]] static PxAwaitable<PxResult<void>> StopAsync(std::shared_ptr<UdpRuntimeState> owner,
                                                               std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] bool IsQuiescent() const;
    void HandleCtrlPacket(const std::shared_ptr<UdpSession>& udp_session, std::span<const char> data);
    void HandleHello(const std::shared_ptr<UdpSession>& udp_session, const std::string& association_code, const std::string& stream_id);
    void HandleHeartbeat(const std::shared_ptr<UdpSession>& udp_session, const std::string& association_code);
    void HandleFrameStatus(uint32_t frame_index, uint16_t received, uint16_t lost);
    void AdjustFecWindow();
    [[nodiscard]] bool SendMediaBatch(std::vector<media::Packet> packets);
    std::atomic_uint64_t stat_batch_wait_max_us_{};
    std::atomic_uint64_t stat_batch_timeouts_{};
    bool HasBoundSession();
    void SweepDeadSessions();
    void UpdateMediaAssociation(const UdpMediaAssociation& association);
    void HandleVoicePacket(const std::shared_ptr<UdpSession>& session, std::span<const char> bytes);
    [[nodiscard]] std::optional<std::pair<std::string, std::string>> VoiceBinding(const std::shared_ptr<UdpSession>& session,
                                                                                  const std::string& association_code);
    [[nodiscard]] bool SendVoiceFrame(const std::string& stream_id, const UdpVoiceFrame& frame);

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
    std::atomic_bool media_send_pending_{false};

  private:
    static PxAwaitable<void> RunHeartbeatSweepLoop(std::weak_ptr<UdpRuntimeState> weak_runtime);
    static PxAwaitable<void> RunFecWindowLoop(std::weak_ptr<UdpRuntimeState> weak_runtime);
    std::shared_ptr<PxAsyncScope> BeginStop();
    void FinishStop();

    RenderEventCallback event_dispatcher_;
    int configured_fec_percent_ = 20;
    std::shared_ptr<PxAsyncScope> control_scope_{};
    std::atomic_bool stopping_{false};
    static constexpr int64_t kHeartbeatTimeoutMs = 10000;
    static constexpr int64_t kUnboundSessionTimeoutMs = 10000;
    static constexpr int kFecMaxPercent = 60;
};

UdpTransport::UdpTransport(std::shared_ptr<PxAsyncRuntime> async_runtime) : async_runtime_(std::move(async_runtime)) {}

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

bool UdpTransport::Start(const RenderModuleConfiguration& configuration) {
    if (!RenderModule::Start(configuration)) {
        return false;
    }
    const int fec_percent = std::clamp(configuration.udp_fec_percent, 0, 100);
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
    const auto runtime = std::make_shared<UdpRuntimeState>(async_runtime_, MakeImmediateEventDispatcher(), fec_percent);
    // Windows sleep 默认 15.6ms 粒度,先把计时器分辨率提到 1ms(高精度 waitable timer 不受此限)
    timer_resolution_active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    // Sunshine 同款高精度 pacing 定时器(Win10 1809+;失败退回普通 waitable timer)
    pace_timer_.reset(CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!pace_timer_) {
        pace_timer_.reset(CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS));
    }
    LOGI("Listen port: {}, fec percent: {}, mtu: {}, pacing: {}Mbps rate-limited (sunshine), timer={}", udp_listen_port_,
         runtime->fec_percent_.load(), udp_mtu_, kRateControlBitsPerSec / 1000000, pace_timer_ ? "ok" : "none");
    if (!runtime->Start(udp_listen_port_)) {
        ReleasePacingResources();
        RenderModule::Stop();
        return false;
    }
    runtime_.store(runtime);
    return true;
}

bool UdpTransport::Destroy() {
    RenderModule::Stop();
    if (const auto runtime = runtime_.load()) {
        runtime->Stop();
        if (runtime->IsQuiescent()) {
            runtime_.store({});
        }
    }
    ReleasePacingResources();
    return RenderModule::Destroy();
}

PxAwaitable<PxResult<void>> UdpTransport::StopAsync(std::shared_ptr<UdpTransport> owner, const std::chrono::steady_clock::time_point deadline) {
    if (!owner) {
        co_return PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "net-udp.stop", "UDP transport owner is missing"));
    }
    owner->RenderModule::Stop();
    const auto runtime = owner->runtime_.load();
    if (runtime) {
        const auto stopped = co_await UdpRuntimeState::StopAsync(runtime, deadline);
        if (!stopped) {
            co_return stopped;
        }
    }
    owner->runtime_.store({});
    owner->ReleasePacingResources();
    co_return PxResult<void>::Success();
}

void UdpTransport::ReleasePacingResources() {
    const std::scoped_lock lock(video_send_mutex_, audio_send_mutex_);
    pace_timer_.reset();
    video_sequences_.clear();
    video_frame_indices_.clear();
    audio_packetizer_.Reset();
    ratecontrol_next_frame_start_ = {};
    if (timer_resolution_active_) {
        timeEndPeriod(1);
        timer_resolution_active_ = false;
    }
}

void UdpTransport::UpdateUdpMediaAssociation(const UdpMediaAssociation& association) {
    if (const auto runtime = runtime_.load()) {
        runtime->UpdateMediaAssociation(association);
    }
}

// Scan the px.Message wire format for AudioFrame.data without importing protobuf into this low-level transport translation unit.
// The returned synchronous view remains valid while msg stays alive and is never retained by asynchronous work.
static std::optional<std::span<const char>> ExtractAudioPayload(const std::shared_ptr<Data>& msg) {
    if (!msg || msg->Size() < 2) {
        return std::nullopt;
    }
    const std::span<const char> bytes{msg->Bytes().data(), static_cast<std::size_t>(msg->Size())};
    std::size_t cursor = 0;
    const auto read_varint = [](const std::span<const char> input, std::size_t& position, std::uint64_t& output) {
        output = 0;
        int shift = 0;
        while (position < input.size() && shift < 64) {
            const auto byte = static_cast<std::uint8_t>(input[position++]);
            output |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
            shift += 7;
        }
        return false;
    };
    bool is_audio = false;
    std::optional<std::span<const char>> audio_frame;
    while (cursor < bytes.size()) {
        std::uint64_t tag = 0;
        if (!read_varint(bytes, cursor, tag)) {
            return std::nullopt;
        }
        const auto field = static_cast<std::uint32_t>(tag >> 3);
        const auto wire = static_cast<std::uint32_t>(tag & 0x7);
        if (field == 10 && wire == 0) {
            std::uint64_t type = 0;
            if (!read_varint(bytes, cursor, type)) {
                return std::nullopt;
            }
            is_audio = type == 40;
            continue;
        }
        if (field == 80 && wire == 2) {
            std::uint64_t length = 0;
            if (!read_varint(bytes, cursor, length) || length > bytes.size() - cursor) {
                return std::nullopt;
            }
            audio_frame = bytes.subspan(cursor, static_cast<std::size_t>(length));
            cursor += static_cast<std::size_t>(length);
            continue;
        }
        switch (wire) {
        case 0: {
            std::uint64_t value = 0;
            if (!read_varint(bytes, cursor, value)) {
                return std::nullopt;
            }
            break;
        }
        case 1:
            cursor += 8;
            break;
        case 2: {
            std::uint64_t length = 0;
            if (!read_varint(bytes, cursor, length) || length > bytes.size() - cursor) {
                return std::nullopt;
            }
            cursor += static_cast<std::size_t>(length);
            break;
        }
        case 5:
            cursor += 4;
            break;
        default:
            return std::nullopt;
        }
        if (cursor > bytes.size()) {
            return std::nullopt;
        }
    }
    if (!is_audio || !audio_frame) {
        return std::nullopt;
    }

    cursor = 0;
    while (cursor < audio_frame->size()) {
        std::uint64_t tag = 0;
        if (!read_varint(*audio_frame, cursor, tag)) {
            return std::nullopt;
        }
        const auto field = static_cast<std::uint32_t>(tag >> 3);
        const auto wire = static_cast<std::uint32_t>(tag & 0x7);
        if (field == 5 && wire == 2) {
            std::uint64_t length = 0;
            if (!read_varint(*audio_frame, cursor, length) || length == 0 || length > audio_frame->size() - cursor) {
                return std::nullopt;
            }
            return audio_frame->subspan(cursor, static_cast<std::size_t>(length));
        }
        switch (wire) {
        case 0: {
            std::uint64_t value = 0;
            if (!read_varint(*audio_frame, cursor, value)) {
                return std::nullopt;
            }
            break;
        }
        case 1:
            cursor += 8;
            break;
        case 2: {
            std::uint64_t length = 0;
            if (!read_varint(*audio_frame, cursor, length) || length > audio_frame->size() - cursor) {
                return std::nullopt;
            }
            cursor += static_cast<std::size_t>(length);
            break;
        }
        case 5:
            cursor += 4;
            break;
        default:
            return std::nullopt;
        }
        if (cursor > audio_frame->size()) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void UdpTransport::Broadcast(std::shared_ptr<Data> msg, bool run_through) {
    static_cast<void>(run_through);
    const auto payload = ExtractAudioPayload(msg);
    const auto runtime = runtime_.load();
    if (!payload || !runtime || !runtime->HasBoundSession())
        return;
    const std::lock_guard lock(audio_send_mutex_);
    const auto packets = audio_packetizer_.Push(
        std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(payload->data()), payload->size()}, static_cast<std::uint16_t>(udp_mtu_));
    for (const auto& bytes : packets) {
        const auto packet = std::make_shared<media::Packet>(bytes);
        runtime->sessions_.ApplyAll([&](const std::string&, const std::shared_ptr<UdpSession>& session) {
            if (!session->bound_ || !session->sess_)
                return;
            session->sess_->async_send(packet->data(), packet->size(), [packet](std::size_t) {});
        });
    }
}

bool UdpTransport::SendToStream(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
    // 空实现:同上
    return false;
}

bool UdpTransport::SendVoiceFrame(const std::string& stream_id, const UdpVoiceFrame& frame) {
    const auto runtime = runtime_.load();
    return runtime && runtime->SendVoiceFrame(stream_id, frame);
}

std::optional<std::pair<std::string, std::string>> UdpRuntimeState::VoiceBinding(const std::shared_ptr<UdpSession>& session,
                                                                                 const std::string& association_code) {
    std::lock_guard lock(bind_mutex_);
    if (stopping_.load() || !session || !session->bound_.load() || session->kicked_.load() || association_code != active_media_association_code_ ||
        session->association_code_ != association_code) {
        return {};
    }
    const auto association = media_associations_.find(association_code);
    const auto current = sessions_.TryGet(session->connection_id_);
    if (association == media_associations_.end() || association->second.endpoint_id_ != session->connection_id_ ||
        association->second.stream_id_ != session->stream_id_ || !current || *current != session) {
        return {};
    }
    return std::pair{association->second.logical_session_id_, association->second.stream_id_};
}

void UdpRuntimeState::HandleVoicePacket(const std::shared_ptr<UdpSession>& session, std::span<const char> bytes) {
    auto frame = UdpVoiceProtocol::Parse(bytes);
    if (!frame) {
        return;
    }
    const auto binding = VoiceBinding(session, frame->association_code);
    if (!binding) {
        return;
    }
    const auto weak_runtime = weak_from_this();
    const auto weak_session = std::weak_ptr(session);
    auto event = std::make_shared<UdpVoiceFrameEvent>();
    event->logical_session_id = binding->first;
    event->stream_id = binding->second;
    event->is_current_binding = [weak_runtime, weak_session, association = frame->association_code, expected = *binding] {
        const auto runtime = weak_runtime.lock();
        const auto session = weak_session.lock();
        return runtime && session && runtime->VoiceBinding(session, association) == expected;
    };
    event->frame = std::make_shared<const UdpVoiceFrame>(std::move(*frame));
    // Dispatch synchronously from this UDP I/O lane: no unbounded reliable/control queue.
    // The receiver revalidates the binding before delivering to the bounded audio endpoint.
    event_dispatcher_(RenderEventEnvelope{.source_id = kNetUdpTransportId, .payload = event});
}

bool UdpRuntimeState::SendVoiceFrame(const std::string& stream_id, const UdpVoiceFrame& frame) {
    std::shared_ptr<UdpSession> session{};
    std::string association_code{};
    {
        std::lock_guard lock(bind_mutex_);
        if (stopping_.load() || stream_id.empty()) {
            return false;
        }
        const auto association = media_associations_.find(active_media_association_code_);
        if (association == media_associations_.end() || association->second.stream_id_ != stream_id) {
            return false;
        }
        const auto current = sessions_.TryGet(association->second.endpoint_id_);
        if (!current || !(*current)->bound_.load() || (*current)->kicked_.load()) {
            return false;
        }
        session = *current;
        association_code = association->first;
    }
    const auto reservation = session->voice_send_budget_.TryAcquire();
    if (!reservation || !session->sess_ || !VoiceBinding(session, association_code)) {
        return false;
    }
    const auto packet = UdpVoiceProtocol::Build(association_code, frame.call_id, frame.sequence, frame.capture_time_ms, frame.opus);
    if (!packet) {
        return false;
    }
    // asio2 submission boundary; retain datagram storage and capacity until callback completion or cancellation.
    session->sess_->async_send(packet->Bytes().data(), packet->Size(), [packet, reservation](std::size_t) {});
    return true;
}

bool UdpRuntimeState::Start(int listen_port) {
    if (!control_scope_ || !control_scope_->IsAccepting()) {
        LOGE("event=module.start component=net_udp code=ASYNC_SCOPE_CREATE_FAILED "
             "operation=start_control_workflows outcome=failed recoverable=false");
        return false;
    }
    stopping_ = false;
    const auto connection_id = [](const std::shared_ptr<asio2::udp_session>& session) {
        return session->remote_address() + ":" + std::to_string(session->remote_port());
    };

    server_ = std::make_shared<asio2::udp_server>();
    const auto weak_runtime = weak_from_this();
    server_
        ->bind_recv([weak_runtime, connection_id](std::shared_ptr<asio2::udp_session>& session, std::string_view data) {
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
            const auto packet_type = PxUdpProtocol::ParseCommon(std::span<const char>{data});
            if (packet_type == PxUdpProtocol::kPktCtrl) {
                runtime->HandleCtrlPacket(opt_sess.value(), std::span<const char>{data});
            } else if (packet_type == PxUdpProtocol::kPktVoice) {
                runtime->HandleVoicePacket(opt_sess.value(), std::span<const char>{data});
            }
        })
        .bind_connect([weak_runtime, connection_id](std::shared_ptr<asio2::udp_session>& session) {
            const auto runtime = weak_runtime.lock();
            if (!runtime) {
                return;
            }
            auto conn_id = connection_id(session);
            auto udp_sess = std::make_shared<UdpSession>();
            udp_sess->connection_id_ = conn_id;
            udp_sess->sess_ = session;
            udp_sess->last_seen_ms_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            runtime->sessions_.Insert(conn_id, udp_sess);
            LOGI("udp client enter : {} {} ; {} {}", session->remote_address().c_str(), session->remote_port(), session->local_address().c_str(),
                 session->local_port());
        })
        .bind_disconnect([weak_runtime, connection_id](auto& session) {
            const auto runtime = weak_runtime.lock();
            if (!runtime) {
                return;
            }
            auto conn_id = connection_id(session);
            std::shared_ptr<UdpSession> removed;
            {
                std::lock_guard lock(runtime->bind_mutex_);
                auto opt_sess =
                    runtime->sessions_.RemoveIf(conn_id, [&](const std::shared_ptr<UdpSession>& cur) { return cur && cur->sess_ == session; });
                if (opt_sess.has_value()) {
                    removed = opt_sess.value();
                    if (removed->bound_.exchange(false)) {
                        runtime->bound_count_--;
                        const auto association = runtime->media_associations_.find(removed->association_code_);
                        if (association != runtime->media_associations_.end() && association->second.endpoint_id_ == conn_id) {
                            association->second.endpoint_id_.clear();
                        }
                        if (runtime->active_media_association_code_ == removed->association_code_) {
                            runtime->active_media_association_code_.clear();
                        }
                    } else {
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
            LOGI("udp media endpoint disconnected: {} {} {}", session->remote_address().c_str(), session->remote_port(),
                 asio2::last_error_msg().c_str());
        })
        .bind_start([weak_runtime]() {
            if (asio2::get_last_error()) {
                LOGE("start udp server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            } else if (const auto runtime = weak_runtime.lock()) {
                LOGI("start udp server success : {} {}", runtime->server_->listen_address().c_str(), runtime->server_->listen_port());
                // 一帧 ~89 个包(~125KB)毫秒内突发下发,默认发送缓冲易满;
                // 发送缓冲调 4MB、接收 1MB,读回值打出来(Windows 上可能与设置值不同)
                asio::error_code ec;
                auto& sock = runtime->server_->acceptor();
                sock.set_option(asio::socket_base::send_buffer_size(4 * 1024 * 1024), ec);
                if (ec)
                    LOGW("udp server set sndbuf 4MB failed: {}", ec.message());
                sock.set_option(asio::socket_base::receive_buffer_size(1 * 1024 * 1024), ec);
                if (ec)
                    LOGW("udp server set rcvbuf 1MB failed: {}", ec.message());
                asio::socket_base::send_buffer_size snd;
                asio::socket_base::receive_buffer_size rcv;
                sock.get_option(snd, ec);
                sock.get_option(rcv, ec);
                LOGI("udp server socket buffer: snd = {}, rcv = {}", snd.value(), rcv.value());
            }
        })
        .bind_stop([]() { LOGI("stop udp server : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str()); })
        .bind_init([]() {

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

    const auto heartbeat_started = control_scope_->Spawn("udp-heartbeat-sweep", [weak_runtime]() { return RunHeartbeatSweepLoop(weak_runtime); });
    const auto fec_started = control_scope_->Spawn("udp-fec-window", [weak_runtime]() { return RunFecWindowLoop(weak_runtime); });
    if (!heartbeat_started || !fec_started) {
        LOGE("event=module.start component=net_udp code=ASYNC_SCOPE_SPAWN_FAILED "
             "operation=start_control_workflows outcome=failed recoverable=false heartbeat_started={} fec_started={}",
             heartbeat_started, fec_started);
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
    const auto remaining = std::max(std::chrono::milliseconds::zero(),
                                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
    if (!adapter_stopped || !scope->WaitFor(remaining)) {
        LOGE("event=async.scope_drain component=net_udp code=ASYNC_SCOPE_DRAIN_TIMEOUT "
             "operation=stop_control_workflows outcome=timeout recoverable=false outstanding={}",
             scope->GetStatistics().outstanding);
        return;
    }
    FinishStop();
}

PxAwaitable<PxResult<void>> UdpRuntimeState::StopAsync(std::shared_ptr<UdpRuntimeState> owner, const std::chrono::steady_clock::time_point deadline) {
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
    if ((server_ && !server_->is_stopped()) || (control_scope_ && control_scope_->GetStatistics().outstanding != 0)) {
        return;
    }
    // Keep the stopped adapter owned until this runtime dies; media producers may still hold this runtime while stopping.
    sessions_.Clear();
    std::scoped_lock lock(bind_mutex_);
    media_associations_.clear();
    active_media_association_code_.clear();
    control_scope_.reset();
}

bool UdpRuntimeState::IsQuiescent() const {
    return (!server_ || server_->is_stopped()) && (!control_scope_ || control_scope_->GetStatistics().outstanding == 0);
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
        LOGI("udp media association revoked: stream={}, remaining={}", association.stream_id_, media_associations_.size());
        return;
    }
    if (association.stream_id_.empty() || association.expires_at_ms_ <= 0) {
        return;
    }
    const auto existing = media_associations_.find(association.association_code_);
    if (existing != media_associations_.end() && existing->second.logical_session_id_ == association.logical_session_id_ &&
        existing->second.stream_id_ == association.stream_id_) {
        // A WS reconnect may refresh the one-time association before the
        // UDP hello arrives. Preserve a successfully bound endpoint.
        existing->second.expires_at_ms_ = association.expires_at_ms_;
        existing->second.force_gdi_ = association.force_gdi_;
        LOGI("udp media association refreshed: stream={}, pending={}", association.stream_id_, media_associations_.size());
        return;
    }
    media_associations_.insert_or_assign(association.association_code_, PendingMediaAssociation{
                                                                            .logical_session_id_ = association.logical_session_id_,
                                                                            .stream_id_ = association.stream_id_,
                                                                            .expires_at_ms_ = association.expires_at_ms_,
                                                                            .force_gdi_ = association.force_gdi_,
                                                                        });
    LOGI("udp media association registered: stream={}, pending={}", association.stream_id_, media_associations_.size());
}

void UdpRuntimeState::HandleCtrlPacket(const std::shared_ptr<UdpSession>& udp_sess, const std::span<const char> data) {
    // kCtrlFrameStatus 是定长二进制体,ParseCtrl 不解析,走专门解析
    uint32_t fs_frame = 0;
    uint16_t fs_received = 0, fs_lost = 0;
    if (PxUdpProtocol::ParseFrameStatus(data, fs_frame, fs_received, fs_lost)) {
        if (udp_sess->bound_) {
            HandleFrameStatus(fs_frame, fs_received, fs_lost);
        }
        return;
    }
    std::string s1, s2;
    auto subtype = PxUdpProtocol::ParseCtrl(data, s1, s2);
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
        auto event = std::make_shared<KeyFrameRequestEvent>();
        event->monitor_name_ = s1;
        event_dispatcher_(RenderEventEnvelope{.source_id = kNetUdpTransportId, .payload = event});
        break;
    }
    case PxUdpProtocol::kCtrlIdrKeepalive: {
        if (!udp_sess->bound_) {
            return;
        }
        // 连接初始化/无帧超时补关键帧:行为同 IDR 请求,但不计入丢帧统计,
        // 否则客户端刚连上自动补几发 IDR 就会把动态 FEC 刷到上限。
        auto event = std::make_shared<KeyFrameRequestEvent>();
        event->monitor_name_ = s1;
        event_dispatcher_(RenderEventEnvelope{.source_id = kNetUdpTransportId, .payload = event});
        break;
    }
    case PxUdpProtocol::kCtrlRfi: {
        if (!udp_sess->bound_) {
            return;
        }
        // s1 = invalid_frame_index(字符串),s2 = mon_name(空=全屏)。
        // 丢整帧后优先走参考帧失效,不插 IDR;不支持 RFI 的编码器由上层忽略,
        // 客户端会在 2s 无完整帧后回退 IDR keepalive。
        auto event = std::make_shared<ReferenceFrameInvalidationEvent>();
        try {
            event->invalid_frame_index_ = std::stoull(s1);
        } catch (...) {
            event->invalid_frame_index_ = 0;
        }
        event->monitor_name_ = s2;
        LOGI("udp rfi request: invalid_frame={}, mon={}", event->invalid_frame_index_, event->monitor_name_);
        event_dispatcher_(RenderEventEnvelope{.source_id = kNetUdpTransportId, .payload = event});
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
    const auto delivered = stat_complete_frames_.exchange(0);
    const auto requests = stat_lost_frames_.exchange(0);
    const auto recovered = stat_recovered_shards_.exchange(0);
    const auto sent = stat_sent_shards_.exchange(0);
    const auto failed = stat_send_short_writes_.exchange(0);
    const auto batch_wait_us = stat_batch_wait_max_us_.exchange(0);
    const auto batch_timeouts = stat_batch_timeouts_.exchange(0);
    if (delivered || requests || sent || failed || batch_timeouts) {
        LOGI("UDP media v2: delivered={}, recovery_requests={}, recovered={}, socket_packets={}, send_failures={}, fixed_fec={}%, "
             "batch_wait_max_us={}, batch_timeouts={}", delivered, requests, recovered, sent, failed, fec_percent_.load(), batch_wait_us,
             batch_timeouts);
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

bool UdpRuntimeState::SendMediaBatch(std::vector<media::Packet> packets) {
    if (stopping_ || !server_ || !server_->is_started() || server_->running_in_this_thread() || media_send_pending_.exchange(true))
        return false;
    const auto completion = std::make_shared<std::promise<bool>>();
    const auto started = std::chrono::steady_clock::now();
    auto result = completion->get_future();
    const auto weak_runtime = weak_from_this();
    server_->post([weak_runtime, completion, packets = std::move(packets)]() {
        const auto runtime = weak_runtime.lock();
        bool succeeded = runtime && !runtime->stopping_ && runtime->server_->is_started();
        if (succeeded) {
            // Socket operations and close run on the adapter IO thread. This is the upstream per-datagram fallback,
            // not an async enqueue measured as a send. At most one bounded video batch is outstanding.
            try {
                runtime->sessions_.ApplyAll([&](const std::string&, const std::shared_ptr<UdpSession>& session) {
                    if (!session->bound_ || !session->sess_)
                        return;
                    for (const auto& packet : packets) {
                        asio::error_code error{};
                        const auto bytes = runtime->server_->acceptor().send_to(asio::buffer(packet), session->sess_->hash_key(), 0, error);
                        if (error || bytes != packet.size()) {
                            ++runtime->stat_send_short_writes_;
                            succeeded = false;
                            break;
                        }
                        ++runtime->stat_sent_shards_;
                    }
                });
            } catch (const std::exception& error) {
                LOGW("UDP media v2 socket batch failed: {}", error.what());
                succeeded = false;
            }
        }
        if (runtime)
            runtime->media_send_pending_ = false;
        completion->set_value(succeeded);
    });
    const auto ready = result.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
    const auto waited = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    auto previous = stat_batch_wait_max_us_.load();
    while (waited > previous && !stat_batch_wait_max_us_.compare_exchange_weak(previous, waited)) {}
    if (!ready)
        ++stat_batch_timeouts_;
    return ready && result.get();
}

void UdpRuntimeState::HandleHello(const std::shared_ptr<UdpSession>& udp_sess, const std::string& association_code, const std::string& stream_id) {
    if (association_code.empty() || stream_id.empty()) {
        LOGW("udp media hello missing association or stream from {}", udp_sess->connection_id_);
        return;
    }
    const auto now = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    std::shared_ptr<UdpSession> replaced_endpoint;
    bool force_gdi = false;
    {
        std::scoped_lock lock(bind_mutex_);
        const auto association_it = media_associations_.find(association_code);
        if (association_it == media_associations_.end() ||
            association_it->second.stream_id_ != stream_id
            // The short expiry gates the first endpoint registration. A
            // bound WS association may subsequently re-register on a NAT
            // port change; WS close remains the authoritative revocation.
            || (association_it->second.expires_at_ms_ <= now && association_it->second.endpoint_id_.empty())) {
            const auto stream_match = std::any_of(media_associations_.begin(), media_associations_.end(),
                                                  [&stream_id](const auto& item) { return item.second.stream_id_ == stream_id; });
            LOGW("udp media hello has no active WS association from {} (stream={}, pending={}, stream_match={})", udp_sess->connection_id_, stream_id,
                 media_associations_.size(), stream_match);
            return;
        }
        if (!active_media_association_code_.empty() && active_media_association_code_ != association_code) {
            LOGW("udp media hello rejected: another endpoint is active");
            return;
        }
        if (udp_sess->bound_ && udp_sess->association_code_ == association_code) {
            udp_sess->last_heartbeat_ms_ = now;
            return;
        }
        if (!association_it->second.endpoint_id_.empty() && association_it->second.endpoint_id_ != udp_sess->connection_id_) {
            const auto previous = sessions_.Remove(association_it->second.endpoint_id_);
            if (previous.has_value()) {
                replaced_endpoint = previous.value();
                if (replaced_endpoint->bound_.exchange(false)) {
                    --bound_count_;
                }
            }
        }
        association_it->second.endpoint_id_ = udp_sess->connection_id_;
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
        replaced_endpoint->sess_->async_send(kick->Bytes().data(), kick->Size(), [kick](std::size_t) {});
    }
    LOGI("udp media endpoint associated: {} stream={}", udp_sess->connection_id_, stream_id);
    // The capture wake caused by WS open can produce its only initial frame
    // before the UDP hello binds an endpoint. A static desktop would then
    // have no later frame to deliver. Re-run the same capture selection only
    // after binding so the first usable encoded frame cannot fall into that
    // gap.
    auto begin_streaming = std::make_shared<StreamingParametersRequestedEvent>();
    begin_streaming->stream_id_ = stream_id;
    begin_streaming->force_gdi_ = force_gdi;
    event_dispatcher_(RenderEventEnvelope{.source_id = kNetUdpTransportId, .payload = begin_streaming});
}

void UdpRuntimeState::HandleHeartbeat(const std::shared_ptr<UdpSession>& udp_sess, const std::string& association_code) {
    if (!udp_sess->bound_ || udp_sess->association_code_ != association_code) {
        return;
    }
    const auto now = static_cast<int64_t>(TimeUtil::GetCurrentTimestamp());
    std::scoped_lock lock(bind_mutex_);
    const auto association_it = media_associations_.find(association_code);
    if (association_it == media_associations_.end() || association_it->second.endpoint_id_ != udp_sess->connection_id_ ||
        active_media_association_code_ != association_code) {
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
            } else if (us->bound_ && now - us->last_heartbeat_ms_.load() > kHeartbeatTimeoutMs) {
                us->bound_ = false;
                bound_count_--;
                const auto association = media_associations_.find(us->association_code_);
                if (association != media_associations_.end() && association->second.endpoint_id_ == us->connection_id_) {
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
        LOGW("udp media session heartbeat timeout: {} (stream: {})", us->connection_id_, us->stream_id_);
        // Media endpoint expiry is diagnostic only: it must never announce
        // a logical client disconnect or release a controller lease.
        static_cast<void>(sessions_.RemoveIf(us->connection_id_, [&](const std::shared_ptr<UdpSession>& cur) { return cur == us; }));
        if (us->sess_) {
            us->sess_->stop();
        }
    }
    for (const auto& us : stale_sessions) {
        LOGW("udp stale session swept: {} (stream: {}, kicked: {})", us->connection_id_, us->stream_id_, us->kicked_.load());
        static_cast<void>(sessions_.RemoveIf(us->connection_id_, [&](const std::shared_ptr<UdpSession>& cur) { return cur == us; }));
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
    LARGE_INTEGER due_time{};
    due_time.QuadPart = ns / -100; // 100ns 单位,负数 = 相对时间
    SetWaitableTimer(pace_timer_.get(), &due_time, 0, nullptr, nullptr,
                     false); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HANDLE boundary
    WaitForSingleObject(pace_timer_.get(),
                        INFINITE); // NOLINT(gammaray-raw-pointer-boundary): transient Win32 HANDLE boundary
}

// data: encode video frame, h264/h265/...
void UdpTransport::SubmitEncodedVideo(const std::string& mon_name, const EncodedVideoType& video_type, const std::shared_ptr<Data>& data,
                                      uint64_t frame_index, int frame_width, int frame_height, bool key, EncodedReferenceState reference_state) {
    const std::lock_guard lock(video_send_mutex_);
    const auto runtime = runtime_.load();
    if (!runtime || !IsWorking() || !data || data->Size() <= 0 || !runtime->HasBoundSession() || frame_width <= 0 || frame_width > 65535 ||
        frame_height <= 0 || frame_height > 65535)
        return;
    if (video_type != EncodedVideoType::kH264 && video_type != EncodedVideoType::kH265)
        return;
    media::VideoFrame frame{};
    frame.codec = video_type == EncodedVideoType::kH265 ? media::VideoCodec::kH265 : media::VideoCodec::kH264;
    frame.kind = key ? media::VideoFrameKind::kIdr : media::VideoFrameKind::kPredicted;
    if (!key && reference_state == EncodedReferenceState::kRecoveryConfirmed)
        frame.kind = media::VideoFrameKind::kReferenceRecovery;
    frame.frame_index = frame_index;
    frame.width = static_cast<std::uint16_t>(frame_width);
    frame.height = static_cast<std::uint16_t>(frame_height);
    frame.stream = MonSlotOf(mon_name);
    frame.monitor = mon_name;
    frame.encoded.assign(data->Bytes().begin(), data->Bytes().end());
    media::VideoPacketParameters parameters{};
    parameters.sequence = video_sequences_[frame.stream];
    parameters.frame_index = ++video_frame_indices_[frame.stream];
    parameters.datagram_size = static_cast<std::uint16_t>(udp_mtu_);
    parameters.fec_percent = static_cast<std::uint8_t>(runtime->fec_percent_.load());
    parameters.timestamp_90khz = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() * 9 / 100);
    auto packetized = media::PacketizeVideoFrame(frame, parameters);
    if (!packetized) {
        LOGW("UDP media v2 frame rejected: index={}, bytes={}, mtu={}", frame_index, data->Size(), udp_mtu_);
        return;
    }
    video_sequences_[frame.stream] = packetized->next_sequence;
    const auto packet_size = static_cast<std::size_t>(udp_mtu_);
    // Keep the upstream time anchor and byte-rate formula, with at most one millisecond of bytes in a burst.
    // The product budget is deliberately lower than upstream's LAN-oriented 800 Mbps parameter.
    const auto packets_per_ms = std::max<std::size_t>(1, kRateControlBitsPerSec / 8 / 1000 / packet_size);
    const auto batch_size = std::min(packets_per_ms, 65536 / packet_size);
    const auto frame_start = std::max(ratecontrol_next_frame_start_, std::chrono::steady_clock::now());
    const auto packet_interval = std::chrono::nanoseconds(1000000000ULL * packet_size * 8 / kRateControlBitsPerSec);
    std::size_t submitted{};
    for (std::size_t offset{}; offset < packetized->packets.size(); offset += batch_size) {
        if (!IsWorking())
            break;
        const auto due = frame_start + packet_interval * submitted;
        const auto now = std::chrono::steady_clock::now();
        if (now < due)
            PaceSleep(due - now);
        const auto count = std::min(batch_size, packetized->packets.size() - offset);
        std::vector<media::Packet> batch{};
        batch.reserve(count);
        for (std::size_t index{}; index < count; ++index)
            batch.push_back(std::move(packetized->packets[offset + index]));
        if (!runtime->SendMediaBatch(std::move(batch)))
            break;
        submitted += count;
    }
    ratecontrol_next_frame_start_ = frame_start + packet_interval * submitted;
    const auto submitted_bytes = submitted * packet_size;
    if (submitted)
        ReportDataSent(static_cast<std::int64_t>(submitted_bytes));
}

int UdpTransport::ConnectedClientCount() const {
    const auto runtime = runtime_.load();
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

} // namespace px
