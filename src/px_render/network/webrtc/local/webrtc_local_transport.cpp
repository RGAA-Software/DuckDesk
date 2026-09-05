//
// Created RGAA on 15/11/2024.
//

#include "webrtc_local_transport.h"
#include "rtc_server.h"
#include "architecture/sources/capture_types.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_common_new/time_util.h"
#include "px_common_new/data.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/async_runtime.h"
#include "px_render/modules/module_ids.h"

#include <functional>
#include <optional>

namespace px {
WebRtcLocalRuntime::WebRtcLocalRuntime(std::weak_ptr<WebRtcLocalTransport> owner, std::weak_ptr<WebRtcExecutionContext> context)
    : owner_(std::move(owner)), context_(std::move(context)) {}

void WebRtcLocalRuntime::WithOwner(const std::function<void(WebRtcLocalTransport&)>& operation) {
    std::scoped_lock lock(owner_mutex_);
    if (const auto owner = owner_.lock(); owner && operation) {
        operation(*owner);
    }
}

bool WebRtcLocalRuntime::IsOwnerActive() const {
    std::scoped_lock lock(owner_mutex_);
    return !owner_.expired();
}

void WebRtcLocalRuntime::DeactivateOwner() {
    std::scoped_lock lock(owner_mutex_);
    owner_.reset();
}

std::shared_ptr<WebRtcExecutionContext> WebRtcLocalRuntime::GetContext() const {
    return context_.lock();
}

void WebRtcLocalRuntime::QueueEvent(WebRtcEvent event, const bool immediate) const {
    const auto context = context_.lock();
    if (!context) {
        return;
    }
    context->Publish(std::move(event), immediate);
}

void WebRtcLocalRuntime::DispatchClientEvent(bool direct, const TransportChannel& channel_type, std::shared_ptr<Data> message,
                                             const std::string& connection_instance_id) {
    QueueEvent(
        WebRtcNetClientEvent{
            .immediate = direct,
            .is_proto = true,
            .message = std::move(message),
            .transport_type = TransportKind::kWebRtc,
            .channel_type = channel_type,
            .connection_instance_id = connection_instance_id,
        },
        direct);
}

void WebRtcLocalRuntime::NotifyTerminal(const std::string& conn_id, const std::shared_ptr<RtcServer>& target) {
    LOGW("Rtc server terminal notified, conn_id: {}, will be swept.", conn_id);
    servers.Apply(conn_id, [target](const std::shared_ptr<RtcServer>& server) {
        if (server == target) {
            server->RequestExit();
        }
    });
}

std::vector<CaptureMonitorInfo> WebRtcLocalRuntime::GetRtcTrackMonitors() {
    std::vector<CaptureMonitorInfo> monitors;
    WithOwner([&](WebRtcLocalTransport& owner) { monitors = owner.GetRtcTrackMonitors(); });
    return monitors;
}

void WebRtcLocalRuntime::EnableAllMonitorCapture() {
    WithOwner([](WebRtcLocalTransport& owner) { owner.EnableAllMonitorCapture(); });
}

void WebRtcLocalRuntime::InsertIdr(const std::string& mon_name) {
    WithOwner([&](WebRtcLocalTransport& owner) { owner.InsertIdr(mon_name); });
}

void WebRtcLocalRuntime::OnRemoteVoiceCallPcm(const std::string& stream_id, const std::string& call_id, const std::span<const std::int16_t> samples,
                                              int sample_rate, int channels) {
    WithOwner([&](WebRtcLocalTransport& owner) { owner.OnRemoteVoiceCallPcm(stream_id, call_id, samples, sample_rate, channels); });
}

uint64_t WebRtcLocalRuntime::GetLatestEncodedSeq(const std::string& mon_name) {
    uint64_t sequence = 0;
    WithOwner([&](WebRtcLocalTransport& owner) { sequence = owner.GetLatestEncodedSeq(mon_name); });
    return sequence;
}

size_t WebRtcLocalRuntime::GetCachedFrameCount(const std::string& mon_name, uint64_t after_seq) {
    size_t count = 0;
    WithOwner([&](WebRtcLocalTransport& owner) { count = owner.GetCachedFrameCount(mon_name, after_seq); });
    return count;
}

std::shared_ptr<RtcLocalEncodedVideoFrame> WebRtcLocalRuntime::ReadNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq,
                                                                                         bool& out_gap) {
    std::shared_ptr<RtcLocalEncodedVideoFrame> frame;
    WithOwner([&](WebRtcLocalTransport& owner) { frame = owner.ReadNextEncodedVideoFrame(mon_name, after_seq, out_gap); });
    return frame;
}

bool WebRtcLocalRuntime::WaitForEncodedFrame(const std::string& mon_name, uint64_t after_seq, int timeout_ms) {
    bool ready = false;
    WithOwner([&](WebRtcLocalTransport& owner) { ready = owner.WaitForEncodedFrame(mon_name, after_seq, timeout_ms); });
    return ready;
}

WebRtcLocalTransport::~WebRtcLocalTransport() {
    Destroy();
}

WebRtcTransportInfo WebRtcLocalTransport::Info() const {
    return WebRtcTransportInfo{
        .id = kNetWebRtcLocalLibraryId,
        .name = "RTC Local",
        .description = "RTC in local mode",
        .version_name = "1.1.0",
        .version_code = 110,
        .enabled = enabled_.load(std::memory_order_acquire),
    };
}

void WebRtcLocalTransport::NotifyRtcServerTerminal(const std::string& conn_id, const std::shared_ptr<RtcServer>& target) {
    runtime_->NotifyTerminal(conn_id, target);
}

void WebRtcLocalTransport::SweepDeadRtcServers() {
    std::vector<std::pair<std::string, std::shared_ptr<RtcServer>>> dead_servers;
    runtime_->servers.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
        if (srv->IsExitRequested()) {
            dead_servers.emplace_back(k, srv);
        }
    });
    for (const auto& [k, srv] : dead_servers) {
        // 只删除扫描到的旧对象；同 key 已被新会话复用时保持新值不动。
        runtime_->servers.RemoveIf(k, [srv](const std::shared_ptr<RtcServer>& current) { return current == srv; });
        LOGI("Sweep dead rtc server: {}", k);
        srv->Exit();
    }
}

bool WebRtcLocalTransport::Start(const WebRtcTransportConfiguration& configuration) {
    if (lifecycle_.load(std::memory_order_acquire) == WebRtcTransportLifecycle::kRunning) {
        return true;
    }
    execution_context_ = WebRtcExecutionContext::Create(configuration.async_runtime, kNetWebRtcLocalLibraryId);
    if (!execution_context_) {
        LOGE("event=webrtc.transport.start component={} code=WEBRTC_RUNTIME_MISSING outcome=failed", kNetWebRtcLocalLibraryId);
        return false;
    }
    runtime_ = std::make_shared<WebRtcLocalRuntime>(weak_from_this(), execution_context_);

    if (!enabled_.load(std::memory_order_acquire)) {
        lifecycle_.store(WebRtcTransportLifecycle::kRunning, std::memory_order_release);
        return true;
    }

    ssl_initialized_ = rtc::InitializeSSL();
    if (!ssl_initialized_) {
        LOGE("RTC Local failed to initialize SSL.");
        return false;
    }

    const auto weak_runtime = std::weak_ptr<WebRtcLocalRuntime>(runtime_);
    if (!execution_context_->StartRepeatingTask(std::chrono::milliseconds(100), [weak_runtime]() {
            if (const auto runtime = weak_runtime.lock()) {
                runtime->servers.ApplyAll([](const std::string&, const std::shared_ptr<RtcServer>& server) { server->On100msTimeout(); });
            }
        })) {
        LOGE("event=webrtc.timer.start component={} interval_ms=100 outcome=failed", kNetWebRtcLocalLibraryId);
        Destroy();
        return false;
    }
    const auto weak_owner = weak_from_this();
    if (!execution_context_->StartRepeatingTask(std::chrono::seconds(1), [weak_owner]() {
            if (const auto owner = weak_owner.lock()) {
                owner->SweepDeadRtcServers();
            }
        })) {
        LOGE("event=webrtc.timer.start component={} interval_ms=1000 outcome=failed", kNetWebRtcLocalLibraryId);
        Destroy();
        return false;
    }

    lifecycle_.store(WebRtcTransportLifecycle::kRunning, std::memory_order_release);
    return true;
}

void WebRtcLocalTransport::Stop() {
    if (lifecycle_.exchange(WebRtcTransportLifecycle::kStopping, std::memory_order_acq_rel) == WebRtcTransportLifecycle::kStopped) {
        return;
    }
    if (execution_context_) {
        execution_context_->BeginStop();
    }
}

void WebRtcLocalTransport::Destroy() {
    Stop();

    const auto runtime = runtime_;
    if (!runtime) {
        lifecycle_.store(WebRtcTransportLifecycle::kStopped, std::memory_order_release);
        return;
    }
    runtime->DeactivateOwner();

    std::vector<std::shared_ptr<RtcServer>> servers;
    runtime->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (srv) {
            servers.push_back(srv);
        }
    });
    runtime->servers.Clear();

    for (const auto& srv : servers) {
        srv->Exit();
    }

    {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        encoded_video_frames_.clear();
        encoded_seq_by_mon_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(idr_request_mtx_);
        last_idr_request_by_mon_.clear();
    }

    if (ssl_initialized_) {
        rtc::CleanupSSL();
        ssl_initialized_ = false;
    }

    runtime_.reset();
    if (execution_context_) {
        static_cast<void>(execution_context_->StopAndWait(std::chrono::seconds(5)));
        execution_context_.reset();
    }
    lifecycle_.store(WebRtcTransportLifecycle::kStopped, std::memory_order_release);
}

void WebRtcLocalTransport::SetEventCallback(WebRtcEventCallback callback) {
    if (execution_context_) {
        execution_context_->SetEventCallback(std::move(callback));
    }
}

void WebRtcLocalTransport::SetEnabled(const bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
}

bool WebRtcLocalTransport::IsWorking() const {
    return enabled_.load(std::memory_order_acquire) && lifecycle_.load(std::memory_order_acquire) == WebRtcTransportLifecycle::kRunning;
}

void WebRtcLocalTransport::UpdateSettings(const WebRtcTransportSettings& settings) {
    settings_ = settings;
}

bool WebRtcLocalTransport::PostWork(std::function<void()> task) const {
    return execution_context_ && execution_context_->PostWork(std::move(task));
}

void WebRtcLocalTransport::ApplyRtcRemoteSdp(const MsgRtcRemoteSdp& message) {
    OnRemoteSdp(message);
}

void WebRtcLocalTransport::ApplyRtcRemoteIce(const MsgRtcRemoteIce& message) {
    OnRemoteIce(message);
}

void WebRtcLocalTransport::ApplyLogicalSessionCapabilities(const PxLogicalSessionCapabilityUpdate& update) {
    const auto runtime = runtime_;
    if (!runtime) {
        return;
    }
    runtime->servers.ApplyAll([&update](const std::string&, const std::shared_ptr<RtcServer>& server) {
        if (server && server->GetStreamId() == update.stream_id_) {
            server->SetPermissions(true, update.permissions_);
        }
    });
}

void WebRtcLocalTransport::OnRemoteSdp(const MsgRtcRemoteSdp& message) {
    const auto weak_runtime = std::weak_ptr<WebRtcLocalRuntime>(runtime_);
    static_cast<void>(PostWork([weak_runtime, message]() {
        const auto runtime = weak_runtime.lock();
        if (!runtime) {
            return;
        }
        // Do not hold runtime->owner_mutex_ while RtcServer::Start().
        // Start queries the same runtime for monitor topology and IDR
        // requests; holding that non-recursive mutex here turns a normal
        // standard-RTC offer into a self-deadlock. The task itself uses no
        // borrowed owner reference, and every owner-dependent runtime call
        // below rechecks owner activity under its own short lock.
        if (!runtime->IsOwnerActive()) {
            return;
        }
        const auto conn_id = message.device_id_ + ":" + message.stream_id_;
        auto send_answer = [weak_runtime, stream_id = message.stream_id_](const std::string& answer_sdp) {
            if (answer_sdp.empty()) {
                LOGE("Standard RTC produced an empty answer, stream={}", stream_id);
                return;
            }
            if (const auto locked = weak_runtime.lock()) {
                locked->QueueEvent(WebRtcAnswerSdpEvent{.stream_id = stream_id, .sdp = answer_sdp});
            }
        };

        if (auto existing = runtime->servers.TryGet(conn_id); existing.has_value()) {
            const auto& server = existing.value();
            server->SetPermissions(true, message.permissions_);
            server->SetOnAnswerCallback(send_answer);
            if (server->RestartWithOffer(message.sdp_, message.ice_config_json_)) {
                LOGI("Reused RTC media peer for standard ICE restart: {}", conn_id);
                return;
            }
            LOGW("Standard RTC in-place restart failed, replacing peer: {}", conn_id);
            runtime->servers.RemoveIf(conn_id, [server](const std::shared_ptr<RtcServer>& current) { return current == server; });
            PxAsyncRuntime::DeferJoin(std::jthread([server]() { server->Exit(); }));
        }

        auto server = RtcServer::Make(runtime);
        server->SetConnId(conn_id);
        server->SetPermissions(true, message.permissions_);
        server->SetOnAnswerCallback(send_answer);
        if (!server->Start(message.stream_id_, message.sdp_, PxLocalRtcSessionRole::kInteractive, message.ice_config_json_)) {
            LOGE("Failed to start standard RTC media peer: {}", conn_id);
            return;
        }
        runtime->servers.Insert(conn_id, server);
        LOGI("Started standard RTC media peer: {}", conn_id);
    }));
}

void WebRtcLocalTransport::OnRemoteIce(const MsgRtcRemoteIce& message) {
    const auto weak_runtime = std::weak_ptr<WebRtcLocalRuntime>(runtime_);
    static_cast<void>(PostWork([weak_runtime, message]() {
        const auto runtime = weak_runtime.lock();
        if (!runtime) {
            return;
        }
        const auto conn_id = message.device_id_ + ":" + message.stream_id_;
        if (auto server = runtime->servers.TryGet(conn_id); server.has_value()) {
            server.value()->OnRemoteIce(message.ice_, message.mid_, message.sdp_mline_index_);
        }
    }));
}

// 视频/音频帧消息不该走 datachannel:RTC 的音视频走 RTP 轨,web 端也不认识
// 这类 proto 消息。但 app 会把每个编码帧广播给所有 net 插件
// (EncodedVideoFanout -> RenderModuleRegistry broadcast),
// 之前照单全收经 SCTP 转发 => ~9Mbps 的 20KB/帧 消息洪水:
// render 每帧 PostTask+memcpy、WaitForMediaChannelActive 在帧分发线程自旋,
// Chrome 主线程每秒 60 次 20KB TLV 重组+proto 解码后丢弃——
// 主线程被淹正是 web 端"帧率低+完全不跟手"的根因(视频 RTP 轨本身健康)。
// wire 级扫描 px.Message 的 type 字段(field 10, varint, tag=0x50),媒体帧直接丢弃。
// 注意:type 不是 field 1;device_id/stream_id 可能在前,必须按 wire 格式逐字段跳过。
static bool IsMediaFrameMessage(const std::shared_ptr<Data>& msg) {
    if (!msg || msg->Size() < 2) {
        return false;
    }
    const auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(msg->DataAddr()),
                                                msg->Size()); // NOLINT(gammaray-raw-pointer-boundary): Data view is wrapped immediately
    const size_t n = msg->Size();
    size_t i = 0;
    auto read_varint = [&](uint64_t& out) -> bool {
        out = 0;
        int shift = 0;
        while (i < n && shift < 64) {
            uint8_t b = bytes[i++];
            out |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) {
                return true;
            }
            shift += 7;
        }
        return false;
    };
    // 逐字段扫描,找到 field 10(type)为止;负载按 wire type 跳过
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
            // px_message.proto: kVideoFrame = 30, kAudioFrame = 40
            return type == 30 || type == 40;
        }
        switch (wire) {
        case 0: {
            uint64_t v;
            if (!read_varint(v)) {
                return false;
            }
            break;
        }
        case 1:
            i += 8;
            break;
        case 2: {
            uint64_t len = 0;
            if (!read_varint(len)) {
                return false;
            }
            i += (size_t)len;
            break;
        }
        case 5:
            i += 4;
            break;
        default:
            return false; // group 等不支持,视为非媒体帧
        }
        if (i > n) {
            return false;
        }
    }
    return false;
}

void WebRtcLocalTransport::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
    if (IsMediaFrameMessage(msg)) {
        return;
    }
    WaitForMediaChannelActive();

    runtime_->servers.ApplyAll(
        [msg, run_through](const std::string&, const std::shared_ptr<RtcServer>& srv) { srv->PostProtoMessage(msg, run_through); });
}

bool WebRtcLocalTransport::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
    if (IsMediaFrameMessage(msg)) {
        return true;
    }
    WaitForMediaChannelActive();

    runtime_->servers.ApplyAll([&stream_id, msg, run_through](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (srv && srv->GetStreamId() == stream_id) {
            srv->PostTargetStreamProtoMessage(stream_id, msg, run_through);
        }
    });
    return true;
}

FileTransferSendResult WebRtcLocalTransport::PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg,
                                                                                bool run_through, const std::string& connection_instance_id) {
    if (!msg) {
        return FileTransferSendResult::TransportError("direct RTC file-transfer payload is empty");
    }
    if (!connection_instance_id.empty()) {
        const auto target = runtime_->servers.TryGet(connection_instance_id);
        if (!target || !*target || (*target)->GetStreamId() != stream_id) {
            return FileTransferSendResult::Disconnected("direct RTC file-transfer session was not found");
        }
        if (!(*target)->IsFtDataChannelConnected()) {
            return FileTransferSendResult::Disconnected("direct RTC file data channel is not connected");
        }
        if ((*target)->GetFtPendingMessages() >= kMaxFileTransferQueuedMessages || !(*target)->HasEnoughBufferForQueuingFtMessages()) {
            return FileTransferSendResult::Busy("direct RTC file data channel is congested", (*target)->AcquireFtWritableSignal());
        }
        return (*target)->PostTargetFileTransferProtoMessage(stream_id, std::move(msg), run_through)
                   ? FileTransferSendResult::Accepted()
                   : FileTransferSendResult::Disconnected("direct RTC file data channel closed before enqueue");
    }
    bool matched = false;
    bool congested = false;
    bool disconnected = false;
    bool accepted = false;
    std::shared_ptr<FileTransferWritableSignal> writable_signal;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (accepted || !srv || srv->GetStreamId() != stream_id) {
            return;
        }
        matched = true;
        if (!srv->IsFtDataChannelConnected()) {
            disconnected = true;
            return;
        }
        if (srv->GetFtPendingMessages() >= kMaxFileTransferQueuedMessages || !srv->HasEnoughBufferForQueuingFtMessages()) {
            congested = true;
            writable_signal = srv->AcquireFtWritableSignal();
            return;
        }
        accepted = srv->PostTargetFileTransferProtoMessage(stream_id, msg, run_through);
        disconnected = !accepted;
    });
    if (accepted) {
        return FileTransferSendResult::Accepted();
    }
    if (congested) {
        return FileTransferSendResult::Busy("direct RTC file data channel is congested", std::move(writable_signal));
    }
    if (matched || disconnected) {
        return FileTransferSendResult::Disconnected("direct RTC file data channel is not connected");
    }
    return FileTransferSendResult::Disconnected("direct RTC file-transfer session was not found");
}

void WebRtcLocalTransport::WaitForMediaChannelActive() {
    if (!runtime_ || runtime_->servers.Empty()) {
        return;
    }
    // 媒体路径最多等 100ms:此函数运行在帧分发线程上,长时间自旋会堵死
    // 整个 render 消息循环。拥塞时直接放行投递(底层 channel 发送失败会自行丢弃)。
    static constexpr int kMaxWaitMs = 100;
    auto queuing_msg_count = GetQueuingMediaMsgCount();
    auto has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
    auto wait_count = 0;
    while ((queuing_msg_count > 256 || !has_buffer) && wait_count < kMaxWaitMs) {
        if (!runtime_ || runtime_->servers.Empty()) {
            LOGW("===> Send media, no alive rtc server, drop the message.");
            return;
        }
        TimeUtil::DelayBySleep(1);
        has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        queuing_msg_count = GetQueuingMediaMsgCount();
        wait_count++;
    }
    // 拥塞日志限频:每 10s 最多一条,避免长时间拥塞时刷爆日志
    static std::atomic<int64_t> last_congestion_log_ts = 0;
    if (wait_count > 0) {
        auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
        auto last = last_congestion_log_ts.load();
        if (now - last >= 10000 && last_congestion_log_ts.compare_exchange_strong(last, now)) {
            LOGW("===> Send media congested, wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        }
    }
}

int WebRtcLocalTransport::GetConnectedClientsCount() {
    int count = 0;
    runtime_->servers.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
        if (!srv->IsWallObserver() && srv->IsDataChannelConnected()) {
            count++;
        }
    });
    return count;
}

int WebRtcLocalTransport::GetMediaConsumersCount() {
    int count = 0;
    runtime_->servers.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
        if (srv && srv->IsMediaConsumerActive()) {
            ++count;
        }
    });
    return count;
}

int64_t WebRtcLocalTransport::GetQueuingMediaMsgCount() {
    uint32_t total_pending_messages = 0;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        // 跳过死连接:其 pending 计数不会再变化,统计进来只会造成误判
        if (!srv->IsDataChannelConnected()) {
            return;
        }
        total_pending_messages += srv->GetMediaPendingMessages();
    });
    return total_pending_messages;
}

int64_t WebRtcLocalTransport::GetQueuingFtMsgCount() {
    uint32_t total_pending_messages = 0;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        // 跳过死连接:其 pending 计数不会再变化,统计进来只会造成误判
        if (!srv->IsFtDataChannelConnected()) {
            return;
        }
        total_pending_messages += srv->GetFtPendingMessages();
    });
    return total_pending_messages;
}

bool WebRtcLocalTransport::HasEnoughBufferForQueuingMediaMessages() {
    bool flag = true;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        // 跳过死连接:死连接的 channel 缓冲永远是满的,会全票否决所有投递
        if (!srv->IsDataChannelConnected()) {
            return;
        }
        flag &= srv->HasEnoughBufferForQueuingMediaMessages();
    });
    return flag;
}

bool WebRtcLocalTransport::HasEnoughBufferForQueuingFtMessages() {
    bool flag = true;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        // 跳过死连接:死连接的 channel 缓冲永远是满的,会全票否决所有投递
        if (!srv->IsFtDataChannelConnected()) {
            return;
        }
        flag &= srv->HasEnoughBufferForQueuingFtMessages();
    });
    return flag;
}

// data: encode video frame, h264/h265/...
void WebRtcLocalTransport::OnEncodedVideoFrame(const std::string& mon_name, const WebRtcEncodedVideoType video_type,
                                               const std::shared_ptr<Data>& data, uint64_t frame_index, int frame_width, int frame_height, bool key) {
    // 诊断:确认编码帧是否到达本插件(每 300 帧打一条)
    static std::atomic_uint64_t encoded_frame_count = 0;
    auto ecnt = ++encoded_frame_count;
    {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);

        auto encoded_video_frame = std::make_shared<RtcLocalEncodedVideoFrame>();
        encoded_video_frame->monitor_name_ = mon_name;
        encoded_video_frame->video_type_ = (int)video_type;
        encoded_video_frame->data_ = data;
        encoded_video_frame->seq_ = ++encoded_seq_by_mon_[mon_name];
        encoded_video_frame->frame_index_ = frame_index;
        encoded_video_frame->frame_width_ = frame_width;
        encoded_video_frame->frame_height_ = frame_height;
        encoded_video_frame->key_ = key;
        encoded_video_frame->timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        encoded_video_frames_.insert({{mon_name, encoded_video_frame->seq_}, encoded_video_frame});

        // 缓存上限:编码快于消费时淘汰该屏最旧帧(未消费就被淘汰的帧会让
        // 消费端发现 seq gap,进而 InsertIdr 等关键帧续接)
        auto first_of_mon = encoded_video_frames_.lower_bound({mon_name, 0});
        auto end_of_mon = encoded_video_frames_.lower_bound({mon_name, UINT64_MAX});
        size_t mon_count = 0;
        for (auto it = first_of_mon; it != end_of_mon; ++it) {
            (void)it;
            ++mon_count;
        }
        while (mon_count > kMaxCachedFramesPerMon && first_of_mon != encoded_video_frames_.end() && first_of_mon->first.first == mon_name) {
            first_of_mon = encoded_video_frames_.erase(first_of_mon);
            --mon_count;
        }
        if (ecnt == 1 || ecnt % 300 == 0) {
            LOGI("OnEncodedVideoFrame #{}, idx={}, key={}, cache={}", ecnt, frame_index, key, encoded_video_frames_.size());
        }
    }

    // 唤醒可能正在 WaitForEncodedFrame 的 webrtc 编码线程(锁外 notify)
    encoded_video_frames_cv_.notify_all();
}

// raw video frame
// handle: D3D Shared texture handle
void WebRtcLocalTransport::OnRawVideoFrameSharedTexture(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height,
                                                        uint64_t handle, int64_t adapter_id, uint64_t frame_format) {
    // 诊断:确认采集帧是否到达本插件(每 300 帧打一条)
    static std::atomic_uint64_t raw_frame_count = 0;
    auto cnt = ++raw_frame_count;
    if (cnt == 1 || cnt % 300 == 0) {
        LOGI("OnRawVideoFrameSharedTexture #{}, idx={}, {}x{}, handle={}, servers={}", cnt, frame_idx, frame_width, frame_height, handle,
             runtime_->servers.Size());
    }
    last_shared_tex_ts_ = TimeUtil::GetCurrentTimestamp();
    runtime_->servers.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& rtc_server) {
        if (!rtc_server || rtc_server->IsExitRequested()) {
            return;
        }
        rtc_server->OnNewFrameCaptured(mon_name, frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
    });
}

// raw video frame in rgba format
// image: Raw image
void WebRtcLocalTransport::OnRawVideoFrameRgba(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height,
                                               const std::shared_ptr<Image>& image) {}

// raw video frame in yuv(I420) format
// image: Raw image
void WebRtcLocalTransport::OnRawVideoFrameYuv(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height,
                                              const std::shared_ptr<Image>& image) {
    // CPU 采集(GDI/mock)没有 shared-texture 事件,WebRTC 的视频源只能从这里
    // 拿到"有帧了"的通知——否则 webrtc 编码线程永远不调 Encode,预编码码流
    // 在缓存里积压到淘汰,客户端黑屏。DDA 路径(含 CPU 编码回退)已由
    // OnRawVideoFrameSharedTexture 喂过,这里抑制避免双倍消费 seq。
    if (TimeUtil::GetCurrentTimestamp() - last_shared_tex_ts_.load() < 1000) {
        return;
    }
    static std::atomic_uint64_t raw_yuv_count = 0;
    auto cnt = ++raw_yuv_count;
    if (cnt == 1 || cnt % 300 == 0) {
        LOGI("OnRawVideoFrameYuv notify webrtc #{}, idx={}, {}x{}, servers={}", cnt, frame_idx, frame_width, frame_height, runtime_->servers.Size());
    }
    runtime_->servers.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& rtc_server) {
        if (!rtc_server || rtc_server->IsExitRequested()) {
            return;
        }
        rtc_server->OnNewRawFrameCaptured(mon_name, frame_idx, frame_width, frame_height);
    });
}

void WebRtcLocalTransport::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {
    if (!data || data->Size() == 0) {
        return;
    }
    static std::atomic_uint64_t audio_cb_count = 0;
    auto cnt = ++audio_cb_count;
    if (cnt == 1 || cnt % 500 == 0) {
        LOGI("OnRawAudioData #{}, bytes={}, rate={} ch={} bits={}, peers={}", cnt, data->Size(), samples, channels, bits, runtime_->servers.Size());
    }
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (!srv || srv->IsExitRequested() || srv->IsWallObserver()) {
            return;
        }
        srv->OnRawAudioData(data, samples, channels, bits);
    });
}

bool WebRtcLocalTransport::SetVoiceCallAuthorization(const std::string& stream_id, const std::string& call_id, bool authorized) {
    bool applied = false;
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (srv && srv->GetStreamId() == stream_id) {
            applied = srv->SetVoiceCallAuthorization(call_id, authorized) || applied;
        }
    });
    return applied;
}

void WebRtcLocalTransport::OnVoiceCallPcm(const std::string& stream_id, const std::string& call_id, const std::span<const std::int16_t> samples,
                                          int sample_rate, int channels) {
    runtime_->servers.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
        if (srv && srv->GetStreamId() == stream_id) {
            srv->OnVoiceCallPcm(call_id, samples, sample_rate, channels);
        }
    });
}

void WebRtcLocalTransport::OnRemoteVoiceCallPcm(const std::string& stream_id, const std::string& call_id, const std::span<const std::int16_t> samples,
                                                int sample_rate, int channels) {
    if (samples.empty()) {
        return;
    }
    execution_context_->Publish(WebRtcVoicePcmEvent{
        .stream_id = stream_id,
        .call_id = call_id,
        .pcm = std::vector<std::int16_t>(samples.begin(), samples.end()),
        .sample_rate = sample_rate,
        .channels = channels,
    });
}

std::shared_ptr<RtcLocalEncodedVideoFrame> WebRtcLocalTransport::ReadNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq,
                                                                                           bool& out_gap) {
    std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
    out_gap = false;
    // 该屏 seq 严格大于 after_seq 的最旧一帧(序消费,保证 H264 delta 链完整)
    auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
    if (it == encoded_video_frames_.end() || it->first.first != mon_name) {
        return nullptr;
    }
    // 最旧可取帧的 seq 跳号:中间有未消费的帧被缓存上限淘汰,delta 链已断
    out_gap = (it->first.second != after_seq + 1);
    // Non-destructive read: every RtcSharedVideoEncoder owns its own cursor.
    // The producer's bounded per-monitor cache evicts old frames, so one
    // slow observer cannot retain memory or steal frames from other peers.
    return it->second;
}

bool WebRtcLocalTransport::WaitForEncodedFrame(const std::string& mon_name, uint64_t after_seq, int timeout_ms) {
    std::unique_lock<std::mutex> lk(encoded_video_frames_mtx_);
    return encoded_video_frames_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]() {
        auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
        return it != encoded_video_frames_.end() && it->first.first == mon_name;
    });
}

void WebRtcLocalTransport::InsertIdr(const std::string& mon_name) {
    if (mon_name.empty()) {
        execution_context_->Publish(WebRtcInsertIdrEvent{});
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(idr_request_mtx_);
        auto last = last_idr_request_by_mon_.find(mon_name);
        if (last != last_idr_request_by_mon_.end() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last->second).count() < kMinIdrRequestIntervalMs) {
            return;
        }
        last_idr_request_by_mon_[mon_name] = now;
    }
    execution_context_->Publish(WebRtcInsertIdrEvent{.monitor_name = mon_name});
}

uint64_t WebRtcLocalTransport::GetLatestEncodedSeq(const std::string& mon_name) {
    std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
    auto it = encoded_seq_by_mon_.find(mon_name);
    return it != encoded_seq_by_mon_.end() ? it->second : 0;
}

size_t WebRtcLocalTransport::GetCachedFrameCount(const std::string& mon_name, uint64_t after_seq) {
    std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
    size_t count = 0;
    for (auto it = encoded_video_frames_.upper_bound({mon_name, after_seq}); it != encoded_video_frames_.end() && it->first.first == mon_name; ++it) {
        ++count;
    }
    return count;
}

void WebRtcLocalTransport::PrintCachedVideoFrames() {
    std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
    for (const auto& [key, frame] : encoded_video_frames_) {
        LOGI("=> mon: {}, seq: {}, key: {}", key.first, key.second, frame->key_);
    }
}

std::vector<CaptureMonitorInfo> WebRtcLocalTransport::GetRtcTrackMonitors() {
    std::vector<CaptureMonitorInfo> result;
    {
        std::scoped_lock lock(capture_topology_mutex_);
        result = capture_topology_;
    }
    // Inner capture has no desktop-monitor plug-in. Its encoded cache is
    // already populated before a client normally asks for an answer, so use
    // those observed sources to keep the answer's track mapping identical to
    // the server's pre-negotiated slots. The later ServerConfiguration is a
    // second, race-safe update path when the first frame arrives after offer.
    if (result.empty()) {
        std::lock_guard<std::mutex> lock(encoded_video_frames_mtx_);
        for (const auto& [key, frame] : encoded_video_frames_) {
            if (!frame || key.first.empty() || frame->frame_width_ <= 0 || frame->frame_height_ <= 0 ||
                (!result.empty() && result.back().name_ == key.first)) {
                continue;
            }
            CaptureMonitorInfo monitor;
            monitor.name_ = key.first;
            monitor.primary_ = result.empty();
            monitor.attached_desktop_ = true;
            monitor.right_ = frame->frame_width_;
            monitor.bottom_ = frame->frame_height_;
            result.push_back(std::move(monitor));
            if (result.size() >= kMaxRtcVideoTracks) {
                break;
            }
        }
        if (!result.empty()) {
            LOGI("RTC track topology recovered from {} observed inner capture source(s)", result.size());
        }
    }
    if ((int)result.size() > kMaxRtcVideoTracks) {
        result.resize(kMaxRtcVideoTracks);
    }
    return result;
}

void WebRtcLocalTransport::EnableAllMonitorCapture() {
    LOGI("Multi-track session: request capture of ALL monitors.");
    execution_context_->Publish(WebRtcSelectCaptureMonitorEvent{.monitor_name = kAllMonitorsNameSign});
}

void WebRtcLocalTransport::UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message) {
    std::scoped_lock lock(capture_topology_mutex_);
    capture_topology_ = message.monitors_;
}

PxLocalRtcAllocResult WebRtcLocalTransport::AllocNewLocalRtcInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& req,
                                                                     std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& callback) {
    auto conn_id = req->device_id_ + ":" + req->stream_id_;
    LOGI("==>AllocNewLocalRtcInstance Offer sdp {} => {}, takeover: {}", conn_id, req->sdp_.size(), req->takeover_);

    const bool is_observer = req->session_role_ == PxLocalRtcSessionRole::kObserver || req->session_role_ == PxLocalRtcSessionRole::kWallObserver;
    static constexpr size_t kMaxWallObservers = 16;

    // Observer sessions coexist with the single interactive connection.
    // They use unique Console-issued stream ids and never participate in the
    // takeover flow.
    std::vector<std::pair<std::string, std::shared_ptr<RtcServer>>> old_servers;
    size_t observer_count = 0;
    runtime_->servers.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
        if (!srv || srv->IsExitRequested()) {
            return;
        }
        if (srv->IsObserver()) {
            ++observer_count;
        } else {
            old_servers.emplace_back(k, srv);
        }
    });
    if (is_observer) {
        const bool duplicate = runtime_->servers.HasKey(conn_id);
        if (observer_count >= kMaxWallObservers || duplicate) {
            LOGW("Reject wall observer, count: {}, duplicate: {}", observer_count, duplicate);
            return PxLocalRtcAllocResult::kFailed;
        }
    } else if (!old_servers.empty()) {
        // 旧连接的 datachannel 仍活跃且调用方未确认接管:报告占用,由客户端决定
        // (web 弹确认后带 takeover=1 重试;原生客户端收到 704 会自动带 takeover 重试)
        if (!req->takeover_) {
            bool occupied = false;
            for (const auto& [k, srv] : old_servers) {
                if (srv->IsDataChannelConnected()) {
                    // 同一浏览器(nonce 非空且相同)重复打开:自动接管旧连接,
                    // 不让用户点确认;nonce 为空(旧客户端/原生)或不同才报占用
                    if (!req->client_nonce_.empty() && req->client_nonce_ == srv->GetClientNonce()) {
                        LOGI("** Auto takeover: same client nonce, kick {}", k);
                        continue;
                    }
                    LOGW("** Occupied by an active connection: {}", k);
                    occupied = true;
                }
            }
            if (occupied) {
                return PxLocalRtcAllocResult::kOccupied;
            }
        }
        if (req->takeover_) {
            // The logical registry has atomically invalidated the old
            // controller lease before this allocation. Preserve the
            // existing RTC media peer as an Observer and revoke its
            // input/file/clipboard capabilities in place. This lets the
            // old Controller continue watching while the new Controller
            // establishes its own peer.
            LOGI("** Demote {} old interactive connection(s) to observer.", old_servers.size());
            for (const auto& [k, srv] : old_servers) {
                static_cast<void>(k);
                if (srv) {
                    srv->DemoteToObserver();
                }
            }
        } else {
            LOGI("** Replace {} old interactive connection(s).", old_servers.size());
            // Same-nonce reconnects are replacements, not a change of
            // role. Remove them before creating the new peer; Exit may
            // block on WebRTC work and must never run on the signaling
            // thread.
            for (const auto& [k, srv] : old_servers) {
                runtime_->servers.RemoveIf(k, [srv](const std::shared_ptr<RtcServer>& current) { return current == srv; });
            }
            PxAsyncRuntime::DeferJoin(std::jthread([old_servers = std::move(old_servers)]() {
                for (const auto& [k, srv] : old_servers) {
                    static_cast<void>(k);
                    if (srv) {
                        srv->Exit();
                    }
                }
            }));
        }
    }

    const auto runtime = runtime_;
    auto rtc_server = RtcServer::Make(runtime);
    rtc_server->SetConnId(conn_id);
    rtc_server->SetClientNonce(req->client_nonce_);
    rtc_server->SetPermissions(req->capability_enforced_, req->permissions_);
    rtc_server->Start(req->stream_id_, req->sdp_, req->session_role_);
    const auto weak_runtime = std::weak_ptr<WebRtcLocalRuntime>(runtime);
    const auto weak_server = std::weak_ptr<RtcServer>(rtc_server);
    rtc_server->SetOnAnswerCallback([weak_runtime, weak_server, req, callback = std::move(callback)](const std::string& answer_sdp) {
        const auto server = weak_server.lock();
        if (!server) {
            return;
        }
        auto answer = server->GetAnswerSdp();
        auto new_answer = AddCandidateIpToAnswer(req->req_ip_, answer);
        auto reply = std::make_shared<PxLocalRtcReplyInfo>(PxLocalRtcReplyInfo{
            .answer_sdp_ = new_answer,
        });
        // 显示器列表(与 video track 同序),多 track 客户端据此做 track→mon_name 映射
        if (const auto locked = weak_runtime.lock()) {
            locked->WithOwner([&](WebRtcLocalTransport& owner) {
                for (const auto& m : owner.GetRtcTrackMonitors()) {
                    reply->monitors_.push_back(PxLocalRtcMonitorInfo{
                        .name_ = m.name_,
                        .width_ = (int)m.Width(),
                        .height_ = (int)m.Height(),
                        .left_ = (int)m.left_,
                        .top_ = (int)m.top_,
                        .right_ = (int)m.right_,
                        .bottom_ = (int)m.bottom_,
                    });
                }
            });
            callback(reply);
        }
    });
    runtime_->servers.Insert(conn_id, rtc_server);
    LOGI("Insert to map, will return information");

    return PxLocalRtcAllocResult::kOk;
}

std::string WebRtcLocalTransport::AddCandidateIpToAnswer(const std::string& ip, const std::string& answer) {
    // std::unique_ptr<webrtc::SessionDescriptionInterface>
    auto session_desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, answer);
    const auto& candidate_collection = *session_desc->candidates(0);

    uint32_t max_priority = 0;
    for (int i = 0; i < candidate_collection.count(); ++i) {
        const auto& ice_candidate = *candidate_collection.at(i);
        const cricket::Candidate& candidate = ice_candidate.candidate();
        if (candidate.priority() > max_priority) {
            max_priority = candidate.priority();
        }
        if (candidate.address().EqualIPs(rtc::SocketAddress(ip, 0))) {
            LOGI("Found same! {}", ip);
            return answer;
        }
    }

    std::vector<std::unique_ptr<webrtc::IceCandidateInterface>> new_ice_candidates;
    for (int i = 0; i < candidate_collection.count(); ++i) {
        const auto& ice_candidate = *candidate_collection.at(i);
        cricket::Candidate candidate = ice_candidate.candidate();

        rtc::SocketAddress address = candidate.address();
        address.SetIP(ip);
        candidate.set_address(address);

        uint32_t udp_priority = static_cast<uint32_t>(std::min(static_cast<uint64_t>(max_priority) + 1, static_cast<uint64_t>(UINT_MAX)));
        uint32_t tcp_priority = static_cast<uint32_t>(std::min(static_cast<uint64_t>(max_priority) + 2, static_cast<uint64_t>(UINT_MAX)));

        if (candidate.protocol() == "udp") {
            candidate.set_priority(udp_priority);
        } else {
            candidate.set_priority(tcp_priority);
        }
        auto new_ice = webrtc::CreateIceCandidate(ice_candidate.sdp_mid(), ice_candidate.sdp_mline_index(), candidate);
        new_ice_candidates.emplace_back(std::move(new_ice));
    }

    for (const auto& new_ice_candidate : new_ice_candidates) {
        session_desc->AddCandidate(new_ice_candidate.get());
        std::string out_string;
        new_ice_candidate->ToString(&out_string);
        LOGI("** AddCandidate {}", out_string);
    }
    std::string sdp;
    if (!session_desc->ToString(&sdp)) {
        LOGE("AddCandidateIpToAnswer failed.");
    }
    return sdp;
}

} // namespace px

namespace px {

std::shared_ptr<WebRtcLocalTransport> CreateWebRtcLocalTransport() {
    return std::make_shared<WebRtcLocalTransport>();
}

} // namespace px
