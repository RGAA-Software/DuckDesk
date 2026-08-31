//
// Created by hy on 20/07/2024.
//

#include "desktop_capture.h"

#include <algorithm>
#include <memory>
#include <libyuv.h>
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/time_util.h"

namespace px
{
    struct DesktopCapture::State final
        : public webrtc::DesktopCapturer::Callback {
        void OnCaptureResult(
            webrtc::DesktopCapturer::Result result,
            std::unique_ptr<webrtc::DesktopFrame> frame) override {
            if (result != webrtc::DesktopCapturer::Result::SUCCESS) {
                LOGE("Capture failed! {}", (int)result);
                return;
            }

            callback_fps++;
            auto timestamp_curr = TimeUtil::GetCurrentTimestamp();
            if (timestamp_curr - last_capture_callback_time > 1000) {
                LOGI("FPS: {}", callback_fps);
                callback_fps = 0;
                last_capture_callback_time = timestamp_curr;
            }

            int width = frame->size().width();
            int height = frame->size().height();
            if (!i420_buffer ||
                i420_buffer->width() * i420_buffer->height() < width * height) {
                i420_buffer = webrtc::I420Buffer::Create(width, height);
            }

            libyuv::ConvertToI420(
                frame->data(), 0, i420_buffer->MutableDataY(),
                i420_buffer->StrideY(), i420_buffer->MutableDataU(),
                i420_buffer->StrideU(), i420_buffer->MutableDataV(),
                i420_buffer->StrideV(), 0, 0, width, height, width,
                height, libyuv::kRotate0, libyuv::FOURCC_ARGB);
            broadcaster.OnFrame(webrtc::VideoFrame(
                i420_buffer, 0, 0, webrtc::kVideoRotation_0));
        }

        rtc::VideoBroadcaster broadcaster;
        cricket::VideoAdapter video_adapter;
        std::unique_ptr<webrtc::DesktopCapturer> capturer;
        size_t fps{};
        std::atomic_bool running{false};
        rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer;
        uint64_t last_capture_callback_time = 0;
        int callback_fps = 0;
    };

    DesktopCapture::DesktopCapture() : state_(std::make_shared<State>()) {

    }

    DesktopCapture::~DesktopCapture() {
        StopCapture();
    }

    std::shared_ptr<DesktopCapture> DesktopCapture::Create(size_t target_fps, size_t capture_screen_index) {
        std::shared_ptr<DesktopCapture> dc(new DesktopCapture());
        if (!dc->Init(target_fps, capture_screen_index)) {
            LOGE("Failed to create DesktopCapture fps = {}, index: {}", target_fps, capture_screen_index);
            return nullptr;
        }
        LOGI("DesktopCapture init success.");
        return dc;
    }

    bool DesktopCapture::Init(size_t target_fps, size_t capture_screen_index) {
        if (target_fps == 0) {
            LOGE("Desktop capture fps must be greater than zero.");
            return false;
        }
        auto options = webrtc::DesktopCaptureOptions::CreateDefault();
        options.set_allow_directx_capturer(true);
        state_->capturer = webrtc::DesktopCapturer::CreateScreenCapturer(options);
        if (!state_->capturer) {
            LOGE("CreateScreenCapture failed!");
            return false;
        }
        state_->capturer->SetMaxFrameRate(target_fps);

        webrtc::DesktopCapturer::SourceList sources;
        state_->capturer->GetSourceList(&sources);
        LOGE("total screen : {}", sources.size());
        if (capture_screen_index >= sources.size()) {
            LOGE("total screen : {}, bit you want to capture: {}", sources.size(), capture_screen_index);
            return false;
        }

        if (!state_->capturer->SelectSource(sources[capture_screen_index].id)) {
            LOGE("Select souce failed,id: {}, title: {}", sources[capture_screen_index].id, sources[capture_screen_index].title);
            return false;
        }
        state_->fps = target_fps;
         LOGI("Init DesktopCapture finish");
        return true;
    }

    void DesktopCapture::AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink, const rtc::VideoSinkWants& wants) {
        state_->broadcaster.AddOrUpdateSink(sink, wants);
        for (auto& item : wants.resolutions) {
            LOGI("item: {}x{}", item.width, item.height);
        }
        state_->video_adapter.OnSinkWants(state_->broadcaster.wants());
    }

    void DesktopCapture::RemoveSink(
            rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
        state_->broadcaster.RemoveSink(sink);
        state_->video_adapter.OnSinkWants(state_->broadcaster.wants());
    }

    void DesktopCapture::StartCapture() {
        if (!state_->capturer) {
            LOGE("Desktop capture is not initialized.");
            return;
        }
        if (state_->running.exchange(true)) {
            LOGE("Desktop capture already started.");
            return;
        }
        const auto state = state_;
        capture_thread_ = std::thread([state]() {
            state->capturer->Start(
                state.get()); // NOLINT(gammaray-raw-pointer-boundary): DesktopCapturer callback ABI; state is retained by worker
            while (state->running) {
                auto beg = TimeUtil::GetCurrentTimestamp();
                state->capturer->CaptureFrame();
                auto end = TimeUtil::GetCurrentTimestamp();
                const auto frame_period = static_cast<int64_t>(1000 / state->fps);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::max<int64_t>(0, frame_period - (end - beg))));
            }
        });
    }

    void DesktopCapture::StopCapture() {
        state_->running = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }

        state_->capturer.reset();
    }

}
