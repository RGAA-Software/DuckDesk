#include "sdk_voice_call.h"

#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_voice_call/voice_audio_format.h"

namespace px {
namespace {

std::uint64_t VoiceNowMillis() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool SendVoiceMessage(const VoiceCallDependencies::MessageSender& send, Message message) {
    try {
        return send(std::make_shared<Message>(std::move(message)));
    } catch (...) {
        return false;
    }
}

void JoinVoiceTimer(std::jthread timer) {
    timer.request_stop();
    if (!timer.joinable()) {
        return;
    }
    if (timer.get_id() == std::this_thread::get_id()) {
        PxAsyncRuntime::DeferJoin(std::move(timer));
    } else {
        timer.join();
    }
}

} // namespace

class VoiceCallController::Run final : public std::enable_shared_from_this<Run> {
  public:
    Run(std::weak_ptr<VoiceCallController> owner, VoiceCallConfig config, VoiceCallDependencies dependencies, std::uint64_t request_id)
        : owner_(std::move(owner)), config_(std::move(config)), dependencies_(std::move(dependencies)), call_id_(GetUUID()), request_id_(request_id) {
        initialized_ = state_.BeginOutgoing(call_id_, request_id_, started_at_, static_cast<std::uint64_t>(config_.request_timeout.count()));
    }

    ~Run() {
        Finish(false, {}, false);
    }

    bool Start() {
        if (!initialized_) {
            Finish(false, "invalid_request");
            return false;
        }
        Publish({});
        if (!SendControl(MakeVoiceCallRequestMessage(config_.device_id, config_.stream_id, call_id_, request_id_, true))) {
            Finish(false, "request_send_failed");
            return false;
        }
        bool expired{};
        {
            std::lock_guard lock(mutex_);
            if (finished_) {
                return false;
            }
            if (state_.Phase() != VoiceCallPhase::kOutgoingPending) {
                return true;
            }
            const auto elapsed = VoiceNowMillis() - started_at_;
            expired = elapsed >= static_cast<std::uint64_t>(config_.request_timeout.count());
            if (!expired) {
                const auto weak = weak_from_this();
                timer_ = std::jthread([weak, timeout = config_.request_timeout - std::chrono::milliseconds(elapsed)](std::stop_token token) {
                    std::mutex mutex{};
                    std::condition_variable_any condition{};
                    std::unique_lock lock(mutex);
                    condition.wait_for(lock, token, timeout, [] { return false; });
                    if (!token.stop_requested()) {
                        if (const auto self = weak.lock()) {
                            self->Timeout();
                        }
                    }
                });
            }
        }
        if (expired) {
            Timeout();
        }
        std::lock_guard lock(mutex_);
        return !finished_;
    }

    VoiceCallStatus Status() const {
        std::lock_guard lock(mutex_);
        auto phase = state_.Phase();
        if (phase == VoiceCallPhase::kConnected && !media_ready_) {
            phase = VoiceCallPhase::kOutgoingPending;
        }
        return {.phase = phase, .microphone_muted = microphone_muted_, .speaker_muted = speaker_muted_};
    }

    bool SetMuted(bool microphone, bool muted) {
        std::shared_ptr<VoiceAudioPort> audio{};
        {
            std::lock_guard lock(mutex_);
            if (finished_ || !media_ready_ || !audio_) {
                return false;
            }
            audio = audio_;
            (microphone ? microphone_muted_ : speaker_muted_) = muted;
        }
        try {
            if (microphone) {
                audio->SetMicrophoneMuted(muted);
            } else {
                audio->SetSpeakerMuted(muted);
            }
        } catch (...) {
            Finish(true, "device_lost");
            return false;
        }
        Publish({});
        return true;
    }

    void HandleMessage(const Message& message) {
        if (message.type() == kVoiceCallRequest && message.has_voice_call_request()) {
            const auto& request = message.voice_call_request();
            if (!request.connect() && request.call_id() == call_id_ && request.request_id() == request_id_) {
                Finish(false, "remote_hangup");
            }
        } else if (message.type() == kVoiceCallResponse && message.has_voice_call_response()) {
            HandleResponse(message.voice_call_response());
        } else if (message.type() == kVoiceAudioConfig && message.has_voice_audio_config()) {
            const auto& config = message.voice_audio_config();
            bool active{};
            {
                std::lock_guard lock(mutex_);
                active = !finished_ && state_.IsMediaAllowed(config.call_id());
            }
            if (active && (config.sample_rate() != VoiceAudioFormat::kSampleRate || config.channels() != VoiceAudioFormat::kChannels ||
                           config.frame_ms() != VoiceAudioFormat::kFrameMs)) {
                Finish(true, "incompatible_audio_config");
            }
        } else if (message.type() == kVoiceAudioFrame && message.has_voice_audio_frame()) {
            Receive(message.voice_audio_frame());
        }
    }

    void Finish(bool notify_remote, std::string reason, bool publish = true) {
        std::shared_ptr<VoiceAudioPort> audio{};
        std::jthread timer{};
        {
            std::lock_guard lock(mutex_);
            if (finished_) {
                return;
            }
            finished_ = true;
            state_.Reset();
            media_ready_ = false;
            microphone_muted_ = false;
            speaker_muted_ = false;
            audio = std::move(audio_);
            timer = std::move(timer_);
        }
        JoinVoiceTimer(std::move(timer));
        packets_.Stop();
        if (audio) {
            try {
                const auto stats = audio->Stats();
                audio->Stop();
                LOGI("[VoiceCall] native run stopped, call={}, reason={}, tx={}, rx={}, plc={}, device_failures={}", VoiceCallLogId(call_id_), reason,
                     stats.encoded_packets, stats.decoded_packets, stats.plc_packets, stats.device_failures);
            } catch (...) {
                LOGW("[VoiceCall] audio port shutdown failed, call={}", VoiceCallLogId(call_id_));
            }
        }
        if (notify_remote) {
            static_cast<void>(SendControl(MakeVoiceCallRequestMessage(config_.device_id, config_.stream_id, call_id_, request_id_, false)));
        }
        if (publish) {
            Publish(std::move(reason));
        }
    }

  private:
    // Send outside the state lock; a synchronous sender/status callback may stop this run.
    // Queued hangup always follows an in-flight connect/config, so cancellation cannot resurrect the peer request.
    bool SendControl(Message message) {
        const auto is_hangup = [](const Message& value) {
            return value.type() == kVoiceCallRequest && value.has_voice_call_request() && !value.voice_call_request().connect();
        };
        {
            std::lock_guard lock(mutex_);
            if (finished_ && !is_hangup(message)) {
                return false;
            }
            control_queue_.push_back(std::move(message));
            if (control_dispatching_) {
                return true;
            }
            control_dispatching_ = true;
        }
        bool accepted{true};
        while (true) {
            Message next{};
            {
                std::lock_guard lock(mutex_);
                if (control_queue_.empty()) {
                    control_dispatching_ = false;
                    return accepted;
                }
                next = std::move(control_queue_.front());
                control_queue_.pop_front();
                if (finished_ && !is_hangup(next)) {
                    continue;
                }
            }
            const auto failure = next.type() == kVoiceAudioConfig ? std::string("config_send_failed") : std::string("request_send_failed");
            if (!SendVoiceMessage(dependencies_.send_control, std::move(next))) {
                accepted = false;
                Finish(false, failure);
            }
        }
    }

    void Publish(std::string reason) {
        if (const auto owner = owner_.lock()) {
            owner->RunChanged(shared_from_this(), std::move(reason));
        }
    }

    void Timeout() {
        bool expired{};
        {
            std::lock_guard lock(mutex_);
            expired = !finished_ && state_.Expire(VoiceNowMillis());
        }
        if (expired) {
            Finish(true, "timeout");
        }
    }

    void HandleResponse(const VoiceCallResponse& response) {
        std::jthread timer{};
        {
            std::lock_guard lock(mutex_);
            if (finished_ || !state_.ApplyResponse(response.call_id(), response.request_id(), response.accepted())) {
                return;
            }
            timer = std::move(timer_);
        }
        JoinVoiceTimer(std::move(timer));
        if (!response.accepted()) {
            Finish(false, response.reason().empty() ? "rejected" : response.reason());
            return;
        }
        std::shared_ptr<VoiceAudioPort> audio{};
        try {
            audio = dependencies_.create_audio();
        } catch (...) {
            audio.reset();
        }
        if (!audio) {
            Finish(true, "no_mic");
            return;
        }
        bool keep{};
        {
            std::lock_guard lock(mutex_);
            keep = !finished_;
            if (keep) {
                audio_ = audio;
            }
        }
        if (!keep) {
            audio->Stop();
            return;
        }
        const auto weak = weak_from_this();
        if (!packets_.Start([weak](const VoiceTransportPacket& packet) {
                if (const auto self = weak.lock()) {
                    self->Dispatch(packet);
                }
            })) {
            Finish(true, "transport_unavailable");
            return;
        }
        std::string error{};
        bool started{};
        try {
            started = audio->Start(
                [weak](VoiceTransportPacket packet) {
                    if (const auto self = weak.lock()) {
                        self->Queue(std::move(packet));
                    }
                },
                [weak](std::string reason) {
                    if (const auto self = weak.lock()) {
                        self->ScheduleFailure(std::move(reason));
                    }
                },
                error);
        } catch (...) {
            error = "audio_start_failed";
        }
        if (!started) {
            Finish(true, error.empty() ? "no_mic" : error);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            keep = !finished_;
        }
        if (!keep) {
            audio->Stop();
            packets_.Stop();
            return;
        }
        if (!SendControl(MakeVoiceAudioConfigMessage(config_.device_id, config_.stream_id, call_id_))) {
            Finish(true, "config_send_failed");
            return;
        }
        {
            std::lock_guard lock(mutex_);
            if (!finished_) {
                media_ready_ = true;
            }
        }
        Publish({});
    }

    void ScheduleFailure(std::string reason) {
        {
            std::lock_guard lock(mutex_);
            if (finished_ || failure_scheduled_) {
                return;
            }
            failure_scheduled_ = true;
        }
        const auto weak = weak_from_this();
        const auto task = [weak, reason = reason.empty() ? std::string("device_lost") : std::move(reason)] {
            if (const auto self = weak.lock()) {
                self->Finish(true, reason);
            }
        };
        bool posted{};
        try {
            posted = dependencies_.post_task(task);
        } catch (...) {
            posted = false;
        }
        if (!posted) {
            // Never synchronously stop a hardware backend from its error callback.
            PxAsyncRuntime::DeferJoin(std::thread(task));
        }
    }

    void Queue(VoiceTransportPacket packet) {
        {
            std::lock_guard lock(mutex_);
            if (finished_ || !media_ready_ || packet.opus.empty() || packet.opus.size() > 1275U) {
                return;
            }
        }
        static_cast<void>(packets_.Enqueue(std::move(packet)));
    }

    void Dispatch(const VoiceTransportPacket& packet) {
        {
            std::lock_guard lock(mutex_);
            if (finished_ || !media_ready_) {
                return;
            }
        }
        static_cast<void>(
            SendVoiceMessage(dependencies_.send_audio, MakeVoiceAudioFrameMessage(config_.device_id, config_.stream_id, call_id_, packet.sequence,
                                                                                  packet.capture_time_ms, packet.opus)));
    }

    void Receive(const VoiceAudioFrame& frame) {
        if (frame.opus().empty() || frame.opus().size() > 1275U) {
            return;
        }
        std::shared_ptr<VoiceAudioPort> audio{};
        {
            std::lock_guard lock(mutex_);
            if (finished_ || !media_ready_ || !state_.AcceptMedia(frame.call_id(), frame.sequence())) {
                return;
            }
            audio = audio_;
        }
        if (audio) {
            try {
                static_cast<void>(audio->Receive({frame.sequence(), frame.capture_time_ms(), {frame.opus().begin(), frame.opus().end()}}));
            } catch (...) {
                ScheduleFailure("audio_receive_failed");
            }
        }
    }

    const std::weak_ptr<VoiceCallController> owner_{};
    const VoiceCallConfig config_{};
    const VoiceCallDependencies dependencies_{};
    const std::string call_id_{};
    const std::uint64_t request_id_{};
    const std::uint64_t started_at_{VoiceNowMillis()};
    mutable std::mutex mutex_{};
    VoiceCallState state_{};
    VoicePacketTransport packets_{};
    std::shared_ptr<VoiceAudioPort> audio_{};
    std::jthread timer_{};
    std::deque<Message> control_queue_{};
    bool control_dispatching_{};
    bool initialized_{};
    bool finished_{};
    bool media_ready_{};
    bool failure_scheduled_{};
    bool microphone_muted_{};
    bool speaker_muted_{};
};

std::shared_ptr<VoiceCallController> VoiceCallController::Create(VoiceCallConfig config, VoiceCallDependencies dependencies) {
    if (config.device_id.empty() || config.stream_id.empty() || config.request_timeout.count() <= 0 ||
        config.request_timeout.count() > VoiceCallState::kRequestTimeoutMs || !dependencies.send_control || !dependencies.send_audio ||
        !dependencies.create_audio || !dependencies.post_task || !dependencies.status_changed) {
        return {};
    }
    return std::make_shared<VoiceCallController>(ConstructionKey{}, std::move(config), std::move(dependencies));
}

VoiceCallController::VoiceCallController(ConstructionKey, VoiceCallConfig config, VoiceCallDependencies dependencies)
    : config_(std::move(config)), dependencies_(std::move(dependencies)) {}

VoiceCallController::~VoiceCallController() {
    Close();
}

void VoiceCallController::SetCapabilities(bool supported, bool requires_headset) {
    std::shared_ptr<Run> stop{};
    std::uint64_t revision{};
    {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        status_.supported = supported;
        status_.requires_headset = requires_headset;
        stop = supported ? std::shared_ptr<Run>{} : active_;
        revision = ++status_.revision;
    }
    if (stop) {
        stop->Finish(true, "unsupported");
    } else {
        ScheduleStatus(revision);
    }
}

void VoiceCallController::SetTransportAvailable(bool available) {
    std::shared_ptr<Run> stop{};
    std::uint64_t revision{};
    {
        std::lock_guard lock(mutex_);
        if (closed_ || transport_available_ == available) {
            return;
        }
        transport_available_ = available;
        stop = available ? std::shared_ptr<Run>{} : active_;
        status_.reason = available ? std::string{} : "media_unavailable";
        revision = ++status_.revision;
    }
    if (stop) {
        stop->Finish(true, "media_unavailable");
    } else {
        ScheduleStatus(revision);
    }
}

bool VoiceCallController::Start() {
    std::shared_ptr<Run> run{};
    std::uint64_t revision{};
    {
        std::lock_guard lock(mutex_);
        if (closed_ || active_) {
            return false;
        }
        if (!status_.supported || !transport_available_) {
            status_.reason = status_.supported ? "media_unavailable" : "unsupported";
            revision = ++status_.revision;
        } else {
            run = std::make_shared<Run>(weak_from_this(), config_, dependencies_, request_sequence_.Next());
            active_ = run;
        }
    }
    if (!run) {
        ScheduleStatus(revision);
        return false;
    }
    return run->Start();
}

void VoiceCallController::HandleMessage(const std::shared_ptr<Message>& message) {
    if (!message || message->device_id() != config_.device_id || message->stream_id() != config_.stream_id) {
        return;
    }
    std::shared_ptr<Run> run{};
    {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        run = active_;
    }
    if (message->type() == kVoiceCallRequest && message->has_voice_call_request() && message->voice_call_request().connect()) {
        const auto& request = message->voice_call_request();
        if (!request.call_id().empty() && request.call_id().size() <= VoiceCallState::kMaxCallIdBytes && request.request_id() != 0) {
            static_cast<void>(
                SendVoiceMessage(dependencies_.send_control, MakeVoiceCallResponseMessage(config_.device_id, config_.stream_id, request.call_id(),
                                                                                          request.request_id(), false, "unsupported_direction")));
        }
    } else if (run) {
        run->HandleMessage(*message);
    }
}

bool VoiceCallController::SetMicrophoneMuted(bool muted) {
    std::shared_ptr<Run> run{};
    {
        std::lock_guard lock(mutex_);
        run = active_;
    }
    return run && run->SetMuted(true, muted);
}

bool VoiceCallController::SetSpeakerMuted(bool muted) {
    std::shared_ptr<Run> run{};
    {
        std::lock_guard lock(mutex_);
        run = active_;
    }
    return run && run->SetMuted(false, muted);
}

void VoiceCallController::Stop(bool notify_remote, std::string reason) {
    std::shared_ptr<Run> run{};
    {
        std::lock_guard lock(mutex_);
        run = active_;
    }
    if (run) {
        run->Finish(notify_remote, std::move(reason));
    }
}

void VoiceCallController::Close() {
    std::shared_ptr<Run> run{};
    {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        run = std::move(active_);
        status_.phase = VoiceCallPhase::kIdle;
        status_.microphone_muted = false;
        status_.speaker_muted = false;
        ++status_.revision;
    }
    if (run) {
        run->Finish(false, {}, false);
    }
}

VoiceCallStatus VoiceCallController::Status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void VoiceCallController::RunChanged(const std::shared_ptr<Run>& run, std::string reason) {
    std::uint64_t revision{};
    {
        std::lock_guard lock(mutex_);
        if (closed_ || active_ != run) {
            return;
        }
        const auto current = run->Status();
        status_.phase = current.phase;
        status_.microphone_muted = current.microphone_muted;
        status_.speaker_muted = current.speaker_muted;
        status_.reason = std::move(reason);
        revision = ++status_.revision;
        if (current.phase == VoiceCallPhase::kIdle) {
            active_.reset();
        }
    }
    ScheduleStatus(revision);
}

void VoiceCallController::ScheduleStatus(std::uint64_t revision) {
    const auto weak = weak_from_this();
    try {
        static_cast<void>(dependencies_.post_task([weak, revision] {
            const auto self = weak.lock();
            if (!self) {
                return;
            }
            VoiceCallStatus status{};
            {
                std::lock_guard lock(self->mutex_);
                if (self->closed_ || self->status_.revision != revision) {
                    return;
                }
                status = self->status_;
            }
            try {
                self->dependencies_.status_changed(status);
            } catch (...) {
                LOGW("[VoiceCall] status consumer threw an exception");
            }
        }));
    } catch (...) {
        LOGW("[VoiceCall] status queue rejected notification");
    }
}

} // namespace px
