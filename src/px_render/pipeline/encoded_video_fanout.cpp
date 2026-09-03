//
// Created by RGAA on 21/11/2024.
//

#include "encoded_video_fanout.h"
#include "rd_context.h"
#include "px_render/modules/render_module_registry.h"
#include "px_capture_new/capture_message.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "app/app_messages.h"
#include "px_render/plugin_interface/px_stream_plugin.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "network/net_message_maker.h"
#include "settings/rd_settings.h"
#include "rd_statistics.h"
#include "rd_app.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/time_util.h"
#include "architecture/observers/frame_debugger_observer.h"
#include "architecture/pipeline/encoded_media_bus.h"
#include "architecture/pipeline/media_types.h"

namespace px
{
    std::shared_ptr<EncodedVideoFanout> EncodedVideoFanout::Make(
        const std::shared_ptr<RdApplication>& app) {
        auto fanout = std::make_shared<EncodedVideoFanout>(app);
        fanout->InitListener();
        return fanout;
    }

    EncodedVideoFanout::EncodedVideoFanout(const std::shared_ptr<RdApplication>& app) {
        app_ = app;
        context_ = app->GetContext();
        module_registry_ = context_->GetRenderModuleRegistry();
        statistics_ = RdStatistics::Instance();
    }

    void EncodedVideoFanout::InitListener() {
        msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
        const auto weak_self = weak_from_this();
        msg_listener_->Listen<CaptureMonitorInfoMessage>([weak_self](const CaptureMonitorInfoMessage&) {
            if (const auto self = weak_self.lock()) {
                self->awaiting_topology_first_frame_.store(true);
            }
        });
    }

    void EncodedVideoFanout::ProcessEncodedVideoFrameEvent(const std::shared_ptr<PxPluginEncodedVideoFrameEvent>& event) {
        const auto& last_capture_video_frame_ = event->capture_frame_;
        if (!event->data_) {
            LOGE("Encoded data is null!");
            return;
        }

        auto frame_index = event->frame_index_;
        auto key = event->key_frame_;
        uint32_t frame_width = event->frame_width_;
        uint32_t frame_height = event->frame_height_;

        // TODO:
        if (event->key_frame_ && 0) {
            LOGI("Encoded: frame size:{}, frame index: {}, key frame: {}, size: {}x{}, monitor: {} - ({},{}, {},{})",
                 event->data_->Size(), frame_index, key, frame_width, frame_height, last_capture_video_frame_.display_name_,
                 last_capture_video_frame_.left_, last_capture_video_frame_.top_, last_capture_video_frame_.right_, last_capture_video_frame_.bottom_);
        } else {
            //LOGI("Encoded frame: {}", frame_index);
        }

        MsgVideoFrameEncoded msg {
            .frame_width_ = static_cast<uint32_t>(frame_width),
            .frame_height_ = static_cast<uint32_t>(frame_height),
            .frame_encode_type_ = (uint32_t)event->type_,
            .frame_index_ = frame_index,
            .key_frame_ = key,
            .data_ = event->data_,
            .monitor_name_ = last_capture_video_frame_.display_name_,
            .monitor_left_ = last_capture_video_frame_.left_,
            .monitor_top_ = last_capture_video_frame_.top_,
            .monitor_right_ = last_capture_video_frame_.right_,
            .monitor_bottom_ = last_capture_video_frame_.bottom_,
            .frame_image_format_ = event->frame_format_,
        };

        const auto observer = context_->GetFrameDebuggerObserver();
        const auto media_bus = context_->GetEncodedMediaBus();
        std::shared_ptr<const render::EncodedVideoFrame> encoded_frame;
        if ((observer && observer->WantsEncodedFrames()) ||
            (media_bus && media_bus->NeedsVideo())) {
            const auto encoded_bytes = event->data_->AsString();
            encoded_frame =
                std::make_shared<const render::EncodedVideoFrame>(
                    render::EncodedVideoFrame{
                        .identity = render::FrameIdentity{
                            .monitor_id = msg.monitor_name_,
                            .frame_index = event->frame_index_,
                            .timestamp_us = static_cast<std::uint64_t>(
                                TimeUtil::GetCurrentTimestamp()) * 1000U,
                        },
                        .codec = event->type_ ==
                                         PxPluginEncodedVideoType::kH264
                                     ? "h264"
                                     : "h265",
                        .width = event->frame_width_,
                        .height = event->frame_height_,
                        .key_frame = event->key_frame_,
                        .payload = render::MakeImmutableByteBuffer(
                            encoded_bytes),
                    });
        }
        if (observer && observer->WantsEncodedFrames() && encoded_frame) {
            static_cast<void>(observer->SubmitEncodedFrame(encoded_frame));
        }
        if (media_bus && encoded_frame) {
            media_bus->PublishVideo(encoded_frame);
        }

        if (awaiting_topology_first_frame_.exchange(false)) {
            context_->SendAppMessage(MsgCaptureTopologyFirstFrame {
                .monitor_name_ = msg.monitor_name_,
            });
        }

        auto video_type = [=]() -> px::VideoType {
            return (Encoder::EncoderFormat)msg.frame_encode_type_ == Encoder::EncoderFormat::kH264 ? px::VideoType::kNetH264 : px::VideoType::kNetHevc;
        } ();

        auto img_format = [=]() -> px::EImageFormat {
            if (RawImageType::kI420 == msg.frame_image_format_) {
                return px::EImageFormat::kI420;
            }
            else if (RawImageType::kI444 == msg.frame_image_format_) {
                return px::EImageFormat::kI444;
            }
            return px::EImageFormat::kI420;
        }();

        auto net_msg = NetMessageMaker::MakeVideoFrameMsg(video_type, msg.data_, msg.frame_index_, msg.frame_width_,
                                                          msg.frame_height_, msg.key_frame_, msg.monitor_name_,
                                                          msg.monitor_left_, msg.monitor_top_, msg.monitor_right_, msg.monitor_bottom_,
                                                          img_format, last_capture_video_frame_.monitor_index_);
        // statistics
        //RdStatistics::Instance()->AppendMediaBytes(net_msg.size());

        // plugins: Frame encoded
        module_registry_->VisitNetworkTransports([&](const std::shared_ptr<PxNetPlugin>& plugin) {
            plugin->PostProtoMessage(net_msg, false);
        });

        const auto module_registry = module_registry_;
        context_->PostMediaTask([module_registry, msg, event]() {
            // net_rtc_local 也是编码帧消费者(WebRTC 复用主编码管线产物,
            // 其 OnEncodedVideoFrame 把帧缓存给 RtcSharedVideoEncoder 取用)。
            // 其它 net 插件不覆写该回调(基类空实现),不受影响。
            module_registry->VisitNetworkTransports([msg, event](const std::shared_ptr<PxNetPlugin>& plugin) {
                plugin->OnEncodedVideoFrame(msg.monitor_name_, event->type_, event->data_, event->frame_index_,
                                            (int)event->frame_width_, (int)event->frame_height_, event->key_frame_);
            });
        });
    }

}
