#include "media_recording_module.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "ct_const_def.h"
#include "media_recorder.h"
#include "px_client/modules/client_module_context.h"
#include "px_client/modules/client_module_services.h"
#include "px_common/log.h"
#include "px_message.pb.h"

namespace px {

class MediaRecordRuntime final {
public:
    explicit MediaRecordRuntime(const std::string& record_path) {
        for (int index = 0; index < kMaxRenderViewCount; ++index) {
            auto recorder = MediaRecorder::Make(record_path);
            recorder->SetIndex(index);
            recorders_.emplace_back(std::move(recorder));
        }
    }

    void Start() {
        std::lock_guard lock(mutex_);
        recording_ = true;
    }

    void OnMessage(const std::shared_ptr<Message>& message) {
        if (!message) {
            return;
        }
        std::lock_guard lock(mutex_);
        if (!recording_) {
            return;
        }
        if (message->type() == MessageType::kVideoFrame) {
            const auto& video_frame = message->video_frame();
            if (video_frame.key()) {
                LOGI("video frame index: {}, {}x{}, key: {}",
                     video_frame.frame_index(), video_frame.frame_width(),
                     video_frame.frame_height(), video_frame.key());
            }
            const auto index = static_cast<std::size_t>(
                std::max(video_frame.mon_index(), 0));
            if (index < recorders_.size()) {
                recorders_[index]->RecvVideoFrame(video_frame);
            } else {
                LOGW("video_frame index: {}, exceeded the maximum limit", index);
            }
        } else if (message->type() == MessageType::kAudioFrame) {
            const auto& audio_frame = message->audio_frame();
            for (const auto& recorder : recorders_) {
                recorder->RecvAudioFrame(audio_frame);
            }
        }
    }

    [[nodiscard]] std::vector<std::string> End() {
        std::lock_guard lock(mutex_);
        recording_ = false;
        std::vector<std::string> recorded_directories;
        for (const auto& recorder : recorders_) {
            if (auto directory = recorder->EndRecord()) {
                recorded_directories.emplace_back(std::move(*directory));
            }
        }
        return recorded_directories;
    }

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<MediaRecorder>> recorders_;
    bool recording_ = false;
};

ClientMediaRecordingModule::ClientMediaRecordingModule(
    std::weak_ptr<ClientModuleServices> services)
    : services_(std::move(services)) {
}

ClientMediaRecordingModule::~ClientMediaRecordingModule() {
    Stop();
}

bool ClientMediaRecordingModule::Start(const ClientModuleConfig& config) {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_) {
        return true;
    }
    stopped_ = false;
    screen_recording_path_ = config.screen_recording_path_;
    context_ = std::make_shared<ClientModuleContext>("client.media_recording");
    runtime_ = std::make_shared<MediaRecordRuntime>(screen_recording_path_);
    LOGI("Built-in Client media-recording module started");
    return true;
}

void ClientMediaRecordingModule::Stop() {
    std::shared_ptr<ClientModuleContext> context;
    std::shared_ptr<MediaRecordRuntime> runtime;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        context = std::move(context_);
        runtime = std::move(runtime_);
    }
    if (context) {
        context->Stop();
    }
    if (runtime) {
        static_cast<void>(runtime->End());
    }
}

void ClientMediaRecordingModule::HandleMessage(
    const std::shared_ptr<Message>& message) {
    if (!message) {
        return;
    }
    std::lock_guard lock(lifecycle_mutex_);
    const auto runtime = runtime_;
    const auto context = context_;
    if (stopped_ || !runtime || !context) {
        return;
    }
    context->PostWorkTask([runtime, message]() {
        runtime->OnMessage(message);
    });
}

void ClientMediaRecordingModule::UpdateSettings(
    const ClientModuleSettings&) {
}

void ClientMediaRecordingModule::StartRecording() {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_ && runtime_) {
        runtime_->Start();
    }
}

void ClientMediaRecordingModule::StopRecording() {
    std::vector<std::string> directories;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_ || !runtime_) {
            return;
        }
        directories = runtime_->End();
    }
    const auto services = services_.lock();
    if (!services) {
        return;
    }
    for (const auto& directory : directories) {
        services->NotifyRecordingComplete(directory);
    }
}

std::string ClientMediaRecordingModule::GetScreenRecordingPath() const {
    std::lock_guard lock(lifecycle_mutex_);
    return screen_recording_path_;
}

}  // namespace px
