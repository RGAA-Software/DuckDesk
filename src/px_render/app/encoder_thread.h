//
// Created by RGAA on 2023-12-24.
//

#ifndef TC_APPLICATION_ENCODER_THREAD_H
#define TC_APPLICATION_ENCODER_THREAD_H

#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include "px_capture/capture_message.h"
#include "settings/rd_settings.h"
#include "px_encoder/encoder_config.h"

namespace px
{
    class Data;
    class Image;
    class File;
    class Thread;
    class RdContext;
    class RdStatistics;
    class VideoEncoderModule;
    class RdApplication;
    class RenderModuleRegistry;
    class MessageListener;
    namespace render {
        class FrameCarrierProcessor;
        class FrameResizerProcessor;
    }

    class EncoderThread : public std::enable_shared_from_this<EncoderThread> {
    public:
        static std::shared_ptr<EncoderThread> Make(const std::shared_ptr<RdApplication>& app);

        explicit EncoderThread(const std::shared_ptr<RdApplication>& app);
        ~EncoderThread();

        void Encode(const CaptureVideoFrame& msg);
        void HandleD3DDeviceFailure(uint64_t adapter_uid);
        void Exit();
        std::map<std::string, std::shared_ptr<VideoEncoderModule>>
            GetWorkingVideoEncoders();

    private:
        void InitListener();
        void EncodeOnWorker(CaptureVideoFrame cap_video_msg,
                            const std::shared_ptr<void>& inflight_guard);
        void PostEncTask(std::function<void()>&& task);
        void PrintEncoderConfig(const px::EncoderConfig& config);
        void ObserveRawFrame(const std::string& monitor_name,
                             std::uint64_t frame_index,
                             std::uint32_t width,
                             std::uint32_t height) const;
        bool HasEncoderForMonitor(const std::string& monitor_name);
        std::shared_ptr<VideoEncoderModule> GetEncoderForMonitor(
            const std::string& monitor_name);

    private:
        RdSettings& settings_;
        std::shared_ptr<RdStatistics> stat_ = nullptr;
        std::shared_ptr<Thread> enc_thread_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        Encoder::EncoderFormat encoder_format_ = Encoder::EncoderFormat::kH264;

        // debug
        std::shared_ptr<File> debug_file_ = nullptr;

        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::mutex encoder_modules_mtx_;
        std::map<std::string, std::shared_ptr<VideoEncoderModule>>
            encoders_;
        std::map<std::string, std::optional<CaptureVideoFrame>> last_video_frames_;
        // Debounce capture size thrash (e.g. game briefly going fullscreen 1920↔3840).
        std::map<std::string, std::pair<uint32_t, uint32_t>> pending_frame_size_;
        std::map<std::string, int64_t> pending_frame_size_since_ms_;

        std::shared_ptr<render::FrameCarrierProcessor> frame_carrier_processor_;
        std::shared_ptr<render::FrameResizerProcessor> frame_resizer_processor_;

        // hardware disabled
        std::atomic_bool hardware_disabled_ = false;

        std::atomic_bool clear_encoders_ = false;
        std::atomic_bool exiting_ = false;
    };

}

#endif //TC_APPLICATION_ENCODER_THREAD_H
