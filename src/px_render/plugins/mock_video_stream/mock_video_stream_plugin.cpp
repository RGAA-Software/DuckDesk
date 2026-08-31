//
// Created RGAA on 15/11/2024.
//

#include "mock_video_stream_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_common_new/image.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"

#include <mutex>
#include <random>
#include <opencv2/opencv.hpp>

namespace px
{
    class MockVideoStreamRuntime final {
    public:
        explicit MockVideoStreamRuntime(PxPluginEventCallback dispatcher)
            : dispatcher_(std::move(dispatcher)) {
        }

        void Start() {
            {
                std::lock_guard lock(mutex_);
                mock_image_ = cv::Mat(height_, width_, CV_8UC4);
                active_ = true;
            }
            Regenerate();
        }

        void Stop() {
            std::lock_guard lock(mutex_);
            active_ = false;
            mock_image_.release(); // NOLINT(gammaray-raw-pointer-boundary) OpenCV buffer release API, not a smart-pointer ownership transfer
        }

        void Regenerate() {
            std::lock_guard lock(mutex_);
            if (!active_ || mock_image_.empty()) {
                return;
            }
            std::uniform_int_distribution<int> channel(0, 255);
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    mock_image_.at<cv::Vec4b>(y, x) = cv::Vec4b(
                        channel(random_), channel(random_),
                        channel(random_), channel(random_));
                }
            }
        }

        void EmitFrame() {
            std::shared_ptr<PxPluginRawVideoFrameEvent> event;
            {
                std::lock_guard lock(mutex_);
                if (!active_ || mock_image_.empty()) {
                    return;
                }
                event = std::make_shared<PxPluginRawVideoFrameEvent>();
                const auto size = static_cast<int64_t>(
                    mock_image_.cols * mock_image_.rows
                    * mock_image_.channels());
                event->image_ = Image::Make(
                    Data::Make(
                        reinterpret_cast<const char*>(mock_image_.data), // NOLINT(gammaray-raw-pointer-boundary) Synchronous OpenCV byte-view boundary
                        size),
                    width_, height_);
                event->frame_index_ = ++frame_index_;
                event->frame_format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
            }
            dispatcher_(event);
        }

    private:
        std::mutex mutex_;
        cv::Mat mock_image_;
        std::mt19937 random_{std::random_device{}()};
        PxPluginEventCallback dispatcher_;
        int width_ = 640;
        int height_ = 480;
        uint64_t frame_index_ = 0;
        bool active_ = false;
    };

    std::string MockVideoStreamPlugin::GetPluginId() {
        return kMockVideoStreamPluginId;
    }

    std::string MockVideoStreamPlugin::GetPluginName() {
        return "Mock Video Frame";
    }

    std::string MockVideoStreamPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t MockVideoStreamPlugin::GetVersionCode() {
        return 110;
    }

    std::string MockVideoStreamPlugin::GetPluginDescription() {
        return "Mock video frame for testing";
    }

    bool MockVideoStreamPlugin::OnCreate(const px::PxPluginParam& param) {
        PxDataProviderPlugin::OnCreate(param);
        runtime_ = std::make_shared<MockVideoStreamRuntime>(
            MakeDirectEventDispatcher());
        return true;
    }

    bool MockVideoStreamPlugin::OnDestroy() {
        PxDataProviderPlugin::OnStop();
        if (runtime_) {
            runtime_->Stop();
            runtime_.reset();
        }
        return PxDataProviderPlugin::OnDestroy();
    }

    void MockVideoStreamPlugin::On1Second() {
        const auto runtime = runtime_;
        PostWorkTask([runtime]() {
            if (runtime) {
                runtime->Regenerate();
            }
        });
    }

    void MockVideoStreamPlugin::StartProviding() {
        const auto runtime = runtime_;
        if (!runtime) {
            return;
        }
        runtime->Start();
        plugin_context_->StartTimer(33, [runtime]() {
            if (runtime) {
                runtime->EmitFrame();
            }
        });
    }

    void MockVideoStreamPlugin::StopProviding() {
        const auto runtime = runtime_;
        if (runtime) {
            runtime->Stop();
        }
    }

}
