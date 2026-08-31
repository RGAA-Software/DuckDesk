//
// Created RGAA on 15/11/2024.
//

#include "media_record_plugin.h"
#include "px_message.pb.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/image.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_client/plugin_interface/ct_plugin_ids.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "px_client/plugin_interface/ct_app_events.h"
#include "media_recorder.h"
#include "ct_const_def.h"

#include <qpushbutton.h>
#include <QPointer>

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

PX_PLUGIN_EXPORT(px::MediaRecordPluginClient)

namespace px
{

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
            if (message->type() == px::kVideoFrame) {
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
                }
                else {
                    LOGW("video_frame index: {}, exceeded the maximum limit",
                         index);
                }
            }
            else if (message->type() == px::kAudioFrame) {
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

    std::string MediaRecordPluginClient::GetPluginId() {
        return kClientMediaRecordPluginId;
    }

    std::string MediaRecordPluginClient::GetPluginName() {
        return "Media Record";
    }

    std::string MediaRecordPluginClient::GetVersionName() {
        return "1.1.0";
    }

    uint32_t MediaRecordPluginClient::GetVersionCode() {
        return 110;
    }

    void MediaRecordPluginClient::On1Second() {
        ClientPluginInterface::On1Second();
    }
    
    bool MediaRecordPluginClient::OnCreate(const px::ClientPluginParam& param) {
        ClientPluginInterface::OnCreate(param);
        plugin_type_ = ClientPluginType::kUtil;

        if (!IsPluginEnabled()) {
            return true;
        }

        runtime_ = std::make_shared<MediaRecordRuntime>(screen_recording_path_);

        root_widget_->hide();
        root_widget_->setWindowTitle("Media Record");
        const QPointer<QVBoxLayout> layout =
            new QVBoxLayout(root_widget_.get()); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        const QPointer<QPushButton> button =
            new QPushButton("Start Record", root_widget_.get()); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        button->setFixedSize(80, 40);
        layout->addWidget(button);

        return true;
    }

    bool MediaRecordPluginClient::OnStop() {
        if (runtime_) {
            (void)runtime_->End();
        }
        return ClientPluginInterface::OnStop();
    }

    bool MediaRecordPluginClient::OnDestroy() {
        if (runtime_) {
            (void)runtime_->End();
            runtime_.reset();
        }
        return ClientPluginInterface::OnDestroy();
    }

    void MediaRecordPluginClient::OnMessage(std::shared_ptr<Message> msg) {
        ClientPluginInterface::OnMessage(msg);
        const auto runtime = runtime_;
        const auto context = plugin_context_;
        if (!runtime || !context || IsStoppingOrDestroyed()) {
            return;
        }
        context->PostWorkTask([runtime, message = std::move(msg)]() {
            runtime->OnMessage(message);
        });
    }

    void MediaRecordPluginClient::DispatchAppEvent(const std::shared_ptr<ClientAppBaseEvent> &event) {
        ClientPluginInterface::DispatchAppEvent(event);
        LOGI("AppEvent: {}", (int)event->evt_type_);
    }

    void MediaRecordPluginClient::StartRecord() {
        if (runtime_) {
            runtime_->Start();
        }
    }

    void MediaRecordPluginClient::EndRecord() {
        if (!runtime_) {
            return;
        }
        const auto directories = runtime_->End();
        const auto weak_context = std::weak_ptr<ClientPluginContext>(plugin_context_);
        for (const auto& directory : directories) {
            auto event = std::make_shared<ClientPluginNotifyMsgEvent>();
            event->title_ = "Screen recording success";
            event->message_ = directory;
            event->clicked_cbk_ = [weak_context, directory]() {
                LOGI("Screen recording ended: {}", directory);
                if (const auto context = weak_context.lock()) {
                    context->PostUITask([directory]() {
                        FolderUtil::OpenDir(PathFromUTF8(directory));
                    });
                }
            };
            CallbackEvent(event);
        }
    }

    std::string MediaRecordPluginClient::GetScreenRecordingPath() const {
        return screen_recording_path_;
    }
}
