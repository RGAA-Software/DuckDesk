//
// Created by RGAA on 2023-12-16.
//

#include "rd_app.h"
#include <filesystem>
#include <windows.h>
#include <future>
#include <random>
#include <thread>
#include "rd_context.h"
#include "px_common/log.h"
#include "px_common/file.h"
#include "px_common/data.h"
#include "px_common/image.h"
#include "px_common/message_notifier.h"
#include "px_common/thread.h"
#include "px_common/process_util.h"
#include "px_common/string_util.h"
#include "px_common/time_util.h"
#include "px_encoder/video_encoder_factory.h"
#include "px_encoder/encoder_messages.h"
#include "px_capture/capture_message.h"
#include "px_capture/capture_message_maker.h"
#include "px_capture/process_loopback_support.h"
#include "app/app_manager.h"
#include "app/app_manager_factory.h"
#include "app/app_messages.h"
#include "settings/rd_settings.h"
#include "render_panel/network/ws_panel_server.h"
#include "app/encoder_thread.h"
#include "network/net_message_maker.h"
#include "px_message.pb.h"
#include "px_render_panel_message.pb.h"
#include "app/app_timer.h"
#include "px_opus_codec/opus_codec.h"
#include "network/ws_panel_client.h"
#include "network/server_cast.h"
#include "app/app_shared_info.h"
#include "app/win/dx_address_loader.h"
#include "px_common/win32/win_helper.h"
#include "px_common/fft_32.h"
#include "px_common/hardware.h"
#include "px_common/shared_preference.h"
#include "px_controller/vigem/vigem_controller.h"
#include "px_controller/vigem_driver_manager.h"
#include "rd_statistics.h"
#include "network/render_service_client.h"
#include "px_render/modules/render_module_registry.h"
#include "px_render/modules/module_ids.h"
#include "architecture/sources/monitor_capture_source.h"
#include "px_service_message.pb.h"
#include "app/win/win_desktop_manager.h"
#include "px_common/win32/d3d11_wrapper.h"
#include "px_message/proto_converter.h"
#include "px_message/rp_proto_converter.h"
#include "px_common/memory_stat.h"
#include "px_common/folder_util.h"
#include "px_common/virtual_display_limits.h"
#include "webview/webview_runtime.h"
#include "session/logical_session_registry.h"
#include "architecture/modules/builtin_module_catalog.h"
#include "architecture/diagnostics/rate_limited_log.h"
#include "architecture/network/network_transport_hub.h"
#include "architecture/observers/frame_debugger_observer.h"
#include "architecture/observers/pipeline_statistics_observer.h"
#include "architecture/pipeline/encoded_media_bus.h"
#include "architecture/pipeline/captured_media_pipeline.h"
#include "architecture/processors/frame_carrier_processor.h"
#include "architecture/processors/frame_resizer_processor.h"
#include "architecture/processors/opus_encoder_processor.h"
#include "architecture/runtime/render_composition_root.h"
#include "architecture/sinks/live_pusher_sink.h"
#include "architecture/sinks/media_recorder_sink.h"
#include "architecture/services/input_replay_service.h"
#include "architecture/services/joystick_service.h"
#include "architecture/services/file_transfer_service.h"
#include "architecture/services/voice_call_service.h"
#include "architecture/sources/was_audio_capture_source.h"
#include "architecture/sinks/live_pusher/live_pusher_ffmpeg.h"

namespace px {

std::shared_ptr<RdApplication> rdApp;

static FpsStat timer_fps;

namespace {

constexpr auto kApplicationShutdownBudget = std::chrono::seconds(15);

PxAwaitable<void> StopApplicationNetworkClients(std::shared_ptr<WsPanelClient> panel_client, std::shared_ptr<RenderServiceClient> service_client,
                                                std::shared_ptr<RenderModuleRegistry> module_registry,
                                                const std::chrono::steady_clock::time_point deadline,
                                                std::shared_ptr<std::promise<PxResult<void>>> completion) {
    if (panel_client) {
        panel_client->Exit();
    }
    if (service_client) {
        service_client->Exit();
    }

    auto outcome = PxResult<void>::Success();
    if (panel_client) {
        const auto stopped = co_await WsPanelClient::StopAsync(panel_client, deadline);
        if (!stopped) {
            outcome = PxResult<void>::Failure(stopped.Error());
        }
    }
    if (service_client) {
        const auto stopped = co_await RenderServiceClient::StopAsync(service_client, deadline);
        if (!stopped && outcome) {
            outcome = PxResult<void>::Failure(stopped.Error());
        }
    }
    if (module_registry) {
        const auto stopped = co_await module_registry->StopNetworkIngressAsync(deadline);
        if (!stopped && outcome) {
            outcome = PxResult<void>::Failure(stopped.Error());
        }
    }
    completion->set_value(std::move(outcome));
}

PxAwaitable<void> StopApplicationWebRtcLibraries(std::shared_ptr<RenderModuleRegistry> module_registry,
                                                 const std::chrono::steady_clock::time_point deadline,
                                                 std::shared_ptr<std::promise<PxResult<void>>> completion) {
    if (!module_registry) {
        completion->set_value(PxResult<void>::Success());
        co_return;
    }
    completion->set_value(co_await module_registry->StopWebRtcLibrariesAsync(deadline));
}

class ApplicationShutdownDispatcher final {
  public:
    static std::shared_ptr<ApplicationShutdownDispatcher> Instance() {
        static const auto instance = std::make_shared<ApplicationShutdownDispatcher>();
        return instance;
    }

    ApplicationShutdownDispatcher() : runtime_(PxAsyncRuntime::Create({.worker_threads = 1})) {
        if (runtime_ && runtime_->Start()) {
            scope_ = PxAsyncScope::Create(runtime_, PxAsyncLane::kWorker);
        }
    }

    ~ApplicationShutdownDispatcher() {
        if (scope_) {
            scope_->BeginStop();
            static_cast<void>(scope_->WaitFor(std::chrono::seconds(5)));
        }
        if (runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
    }

    [[nodiscard]] bool Submit(const std::shared_ptr<RdApplication>& application) const {
        return scope_ && scope_->Spawn("application-root-shutdown", [application] { return Run(application); });
    }

  private:
    static PxAwaitable<void> Run(std::shared_ptr<RdApplication> application) {
        application->Exit();
        co_return;
    }

    std::shared_ptr<PxAsyncRuntime> runtime_{};
    std::shared_ptr<PxAsyncScope> scope_{};
};

} // namespace

std::shared_ptr<RdApplication> RdApplication::Make(const AppParams& args) {
    struct WinApplicationEnabler final : WinApplication {
        explicit WinApplicationEnabler(const AppParams& app_args) : WinApplication(app_args) {}
    };

    // By OS
    // Windows
    return std::make_shared<WinApplicationEnabler>(args);
    // Linux
}

RdApplication::RdApplication(const AppParams& args) {
    auto settings = RdSettings::Instance();
    settings_ = settings;
    logical_session_registry_ = std::make_shared<LogicalSessionRegistry>();

    // debug
    // MessageBoxA(0, "", "debug", 0);
}

RdApplication::~RdApplication() {
    Exit();
    LOGI("RdApplication dtor");
}

void RdApplication::Init(int argc, char** argv) {
    init_failed_ = false;
    init_error_.clear();

    // sp
    sp_ = SharedPreference::Instance();
    auto path = FolderUtil::GetProgramDataPath() + L"/px_data";
    std::string sp_name = std::format("pixels_render_{}.dat", settings_->transmission_.listening_port_);
    if (!sp_->Init(std::filesystem::path{path}, sp_name)) {
        init_failed_ = true;
        init_error_ =
            std::format("Init render SharedPreference failed, path: {}, file: {}, error: {}", StringUtil::ToUTF8(path), sp_name, sp_->GetLastError());
        LOGE("{}", init_error_);
    }
}

int RdApplication::Run() {
    if (init_failed_) {
        LOGE("RdApplication init failed, abort run: {}", init_error_);
        return -1;
    }
    statistics_ = RdStatistics::Instance();

    // context
    context_ = std::make_shared<RdContext>();
    context_->Init();
    const std::weak_ptr<RdApplication> weak_application = weak_from_this();

    const auto builtin_catalog = render::BuiltinModuleCatalog::Create();
    composition_root_ = render::RenderCompositionRoot::Create(context_->GetAsyncRuntime(), builtin_catalog);
    encoded_media_bus_ = render::EncodedMediaBus::Create();
    pipeline_error_log_gate_ = std::make_shared<render::RateLimitedLogGate>(std::chrono::seconds(5), 16);
    captured_media_pipeline_ = render::CapturedMediaPipeline::Create(
        [weak_application](const std::shared_ptr<const render::CapturedVideoFrame>& frame) {
            const auto application = weak_application.lock();
            return application ? application->DeliverExtensionVideoFrame(frame)
                               : render::MediaSubmitResult(std::unexpected(render::RenderError{
                                     .code = render::RenderErrorCode::kModuleDependencyUnavailable,
                                     .component = "rd_application",
                                     .operation = "deliver_extension_video",
                                     .stage = "capture_output",
                                     .reason = "application owner expired",
                                     .recoverable = true,
                                 }));
        },
        [weak_application](const std::shared_ptr<const render::CapturedAudioFrame>& frame) {
            const auto application = weak_application.lock();
            return application ? application->DeliverCapturedAudioFrame(frame)
                               : render::MediaSubmitResult(std::unexpected(render::RenderError{
                                     .code = render::RenderErrorCode::kModuleDependencyUnavailable,
                                     .component = "rd_application",
                                     .operation = "deliver_captured_audio",
                                     .stage = "capture_output",
                                     .reason = "application owner expired",
                                     .recoverable = true,
                                 }));
        });
    frame_debugger_observer_ = render::FrameDebuggerObserver::Create(
        context_->GetAsyncRuntime(), render::FrameDebuggerOptions{
                                         .queue_capacity = 120,
                                         .save_encoded_video = false,
                                         .output_directory = std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"px_data" / L"render",
                                         .raw_log_interval = std::chrono::seconds(1),
                                     });
    const std::weak_ptr<RdContext> weak_context = context_;
    auto record_directory = settings_->record_dir_;
    if (record_directory.empty()) {
        record_directory = (std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"px_render_records").string();
    }
    media_recorder_sink_ = render::MediaRecorderSink::Create(encoded_media_bus_,
                                                             render::MediaRecorderOptions{
                                                                 .record_directory = std::move(record_directory),
                                                                 .auto_enabled = settings_->record_auto_,
                                                                 .max_segment_bytes = settings_->record_max_segment_bytes_,
                                                                 .max_file_count = settings_->record_max_file_count_,
                                                                 .queue_capacity = 512,
                                                             },
                                                             [weak_context] {
                                                                 if (const auto context = weak_context.lock()) {
                                                                     context->SendAppMessage(MsgInsertIDR{});
                                                                 }
                                                             });
    const auto push_configuration_valid = !settings_->push_rtmp_url_.empty() && !settings_->live_stream_id_.empty();
    if (settings_->push_enabled_ && !push_configuration_valid) {
        LOGE("event=module.configure component=live_pusher "
             "outcome=disabled reason=missing_url_or_stream_id");
    }
    live_pusher_sink_ = render::LivePusherSink::Create(
        encoded_media_bus_,
        render::LivePusherOptions{
            .enabled = settings_->push_enabled_ && push_configuration_valid,
            .publish_url = render::BuildLivePublishUrl(settings_->push_rtmp_url_, settings_->live_stream_id_),
            .primary_monitor = settings_->push_primary_monitor_,
            .audio_bitrate = settings_->push_audio_bitrate_,
            .queue_capacity = 48,
        },
        [weak_context] {
            if (const auto context = weak_context.lock()) {
                context->SendAppMessage(MsgInsertIDR{});
            }
        },
        render::MakeFfmpegLivePushProcessor);
    pipeline_statistics_observer_ = render::PipelineStatisticsObserver::Create(encoded_media_bus_);
    frame_carrier_processor_ = render::FrameCarrierProcessor::Create(RdContext::GetCurrentExeFolder());
    frame_resizer_processor_ = render::FrameResizerProcessor::Create();
    network_transport_hub_ = render::NetworkTransportHub::Create(
        [weak_application](const render::TransportRoute& route, const std::shared_ptr<Data>& message, const bool run_through) {
            const auto application = weak_application.lock();
            const auto manager = application ? application->GetRenderModuleRegistry() : std::shared_ptr<RenderModuleRegistry>{};
            return manager && manager->SendControlMessageOnRoute(route.transport_id, route.stream_id, message, run_through);
        },
        [weak_application](const render::TransportRoute& route, const std::shared_ptr<Data>& message) {
            const auto application = weak_application.lock();
            const auto manager = application ? application->GetRenderModuleRegistry() : std::shared_ptr<RenderModuleRegistry>{};
            return manager ? manager->SendFileTransferMessageOnRoute(route.transport_id, route.stream_id, message, route.connection_id)
                           : FileTransferSendResult::Disconnected("Render network layer is unavailable");
        },
        [weak_application](const render::TransportRoute& route, const std::shared_ptr<Data>& message) {
            const auto application = weak_application.lock();
            const auto manager = application ? application->GetRenderModuleRegistry() : std::shared_ptr<RenderModuleRegistry>{};
            return manager && manager->SendVoiceMessageOnRoute(route.transport_id, route.stream_id, message);
        },
        [weak_application](const render::TransportRoute& route, const std::string& call_id, const bool authorized) {
            const auto application = weak_application.lock();
            const auto manager = application ? application->GetRenderModuleRegistry() : std::shared_ptr<RenderModuleRegistry>{};
            return manager && manager->SetRtcVoiceAuthorizationOnRoute(route.stream_id, call_id, authorized);
        },
        [weak_application](const render::TransportRoute& route, const std::string& call_id,
                           const std::shared_ptr<const std::vector<std::int16_t>>& samples, const int sample_rate, const int channels) {
            const auto application = weak_application.lock();
            const auto manager = application ? application->GetRenderModuleRegistry() : std::shared_ptr<RenderModuleRegistry>{};
            return manager && manager->SendRtcVoicePcmOnRoute(route.stream_id, call_id, samples, sample_rate, channels);
        });
    opus_encoder_processor_ =
        render::OpusEncoderProcessor::Create(encoded_media_bus_, [weak_application](const std::shared_ptr<const render::EncodedAudioFrame>& frame) {
            const auto application = weak_application.lock();
            if (!application || application->exit_app_ || !frame || !frame->payload) {
                return;
            }
            const auto data = Data::From(render::ImmutableByteBufferAsString(frame->payload));
            application->PostNetMessage(NetMessageMaker::MakeAudioFrameMsg(data, static_cast<int>(frame->samples), static_cast<int>(frame->channels),
                                                                           static_cast<int>(frame->bits_per_sample),
                                                                           static_cast<int>(frame->frame_size)));
        });
    audio_capture_source_ = render::WasAudioCaptureSource::Create([weak_application](const CaptureAudioFrame& frame) {
        const auto application = weak_application.lock();
        if (!application || application->exit_app_) {
            return;
        }
        application->PostGlobalTask([weak_application, frame] {
            if (const auto active_application = weak_application.lock(); active_application && !active_application->exit_app_) {
                active_application->OnCapturedAudioFrame(frame);
            }
        });
    });
    input_replay_service_ = render::InputReplayService::Create();
    joystick_service_ = render::JoystickService::Create();
    file_transfer_service_ = render::FileTransferService::Create(
        render::FileTransferServiceOptions{
            .device_id = settings_->device_id_,
            .enabled = settings_->file_transfer_enabled_,
            .max_transmit_speed_bits_per_second = settings_->max_transmit_speed_,
        },
        [weak_hub = std::weak_ptr<render::NetworkTransportHub>(network_transport_hub_)](
            const std::string& transport_id, const std::string& stream_id, const std::shared_ptr<Data>& message, const std::string& connection_id) {
            const auto hub = weak_hub.lock();
            return hub ? hub->SendFileTransfer(
                             render::TransportRoute{
                                 .channel = render::TransportChannelKind::kFileTransfer,
                                 .transport_id = transport_id,
                                 .connection_id = connection_id,
                                 .stream_id = stream_id,
                             },
                             message)
                       : FileTransferSendResult::Disconnected("Render network transport hub is unavailable");
        },
        [weak_application](const render::FileTransferAuditBegin& audit) {
            if (const auto application = weak_application.lock()) {
                application->ReportFileTransferAuditBegin(audit);
            }
        },
        [weak_application](const render::FileTransferAuditEnd& audit) {
            if (const auto application = weak_application.lock()) {
                application->ReportFileTransferAuditEnd(audit);
            }
        });
    voice_call_service_ = render::VoiceCallService::Create(
        settings_->voice_call_enabled_,
        [weak_application](std::function<void()>&& task) {
            if (const auto application = weak_application.lock()) {
                application->PostGlobalTask(std::move(task));
            }
        },
        [weak_application](const render::VoiceCallConsentNotice& notice) {
            const auto application = weak_application.lock();
            if (!application) {
                return false;
            }
            pxrp::RpMessage message;
            if (notice.show) {
                message.set_type(pxrp::kRpVoiceCallConsentRequest);
                auto& request = *message.mutable_voice_call_consent_request();
                request.set_visitor_device_id(notice.visitor_device_id);
                request.set_stream_id(notice.stream_id);
                request.set_call_id(notice.call_id);
                request.set_request_id(notice.request_id);
                request.set_expires_at_unix_ms(notice.expires_at_unix_ms);
                request.set_protocol_version(1);
            } else {
                message.set_type(pxrp::kRpVoiceCallConsentCancel);
                auto& cancel = *message.mutable_voice_call_consent_cancel();
                cancel.set_stream_id(notice.stream_id);
                cancel.set_call_id(notice.call_id);
                cancel.set_request_id(notice.request_id);
                cancel.set_reason(notice.reason);
            }
            // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): synchronous protobuf conversion.
            return application->PostPanelMessage(RpProtoAsData(&message));
        },
        [weak_hub = std::weak_ptr<render::NetworkTransportHub>(network_transport_hub_)](const render::TransportRoute& route,
                                                                                        const std::shared_ptr<Data>& message) {
            const auto hub = weak_hub.lock();
            return hub && hub->SendVoice(route, message);
        },
        [weak_hub = std::weak_ptr<render::NetworkTransportHub>(network_transport_hub_)](const render::TransportRoute& route,
                                                                                        const std::string& call_id, const bool authorized) {
            const auto hub = weak_hub.lock();
            return hub && hub->SetRtcVoiceAuthorization(route, call_id, authorized);
        },
        [weak_hub = std::weak_ptr<render::NetworkTransportHub>(network_transport_hub_)](
            const render::TransportRoute& route, const std::string& call_id, const std::shared_ptr<const std::vector<std::int16_t>>& samples,
            const int sample_rate, const int channels) {
            const auto hub = weak_hub.lock();
            return hub && hub->SendRtcVoicePcm(route, call_id, samples, sample_rate, channels);
        });
    if (!composition_root_ || !encoded_media_bus_ || !captured_media_pipeline_ || !frame_debugger_observer_ || !media_recorder_sink_ ||
        !live_pusher_sink_ || !pipeline_statistics_observer_) {
        init_failed_ = true;
        init_error_ = "Create Render composition root failed";
        LOGE("event=composition.create component=rd_application "
             "code=MODULE_DEPENDENCY_UNAVAILABLE outcome=failed");
        return -1;
    }
    if (!frame_carrier_processor_ || !frame_resizer_processor_ || !opus_encoder_processor_ || !audio_capture_source_ || !input_replay_service_ ||
        !joystick_service_ || !file_transfer_service_ || !network_transport_hub_ || !voice_call_service_) {
        init_failed_ = true;
        init_error_ = "Create frame processing modules failed";
        return -1;
    }
    auto debugger_registration = frame_debugger_observer_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(debugger_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kFrameDebuggerModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto recorder_registration = media_recorder_sink_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(recorder_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kMediaRecorderModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto pusher_registration = live_pusher_sink_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(pusher_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kLivePusherModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto statistics_registration = pipeline_statistics_observer_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(statistics_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kPipelineStatisticsModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto resizer_registration = frame_resizer_processor_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(resizer_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kFrameResizerModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto carrier_registration = frame_carrier_processor_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(carrier_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kFrameCarrierModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto opus_registration = opus_encoder_processor_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(opus_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kOpusEncoderModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto input_registration = input_replay_service_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(input_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kInputReplayModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto audio_source_registration = audio_capture_source_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(audio_source_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kWasAudioCaptureModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto joystick_registration = joystick_service_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(joystick_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kJoystickModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto file_transfer_registration = file_transfer_service_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(file_transfer_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kFileTransferModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    auto voice_call_registration = voice_call_service_->MakeRegistration();
    if (auto registered = composition_root_->Register(std::move(voice_call_registration)); !registered) {
        init_failed_ = true;
        init_error_ = registered.error().reason;
        LOGE("event=module.register component=rd_application module={} "
             "code={} outcome=failed reason={}",
             render::kVoiceCallModuleId, render::StableErrorCode(registered.error().code), registered.error().reason);
        return -1;
    }
    context_->SetRenderCompositionRoot(composition_root_);
    context_->SetEncodedMediaBus(encoded_media_bus_);
    context_->SetFrameDebuggerObserver(frame_debugger_observer_);
    context_->SetMediaRecorderSink(media_recorder_sink_);
    context_->SetFrameCarrierProcessor(frame_carrier_processor_);
    context_->SetFrameResizerProcessor(frame_resizer_processor_);
    context_->SetInputReplayService(input_replay_service_);
    context_->SetJoystickService(joystick_service_);
    context_->SetFileTransferService(file_transfer_service_);
    context_->SetNetworkTransportHub(network_transport_hub_);
    context_->SetVoiceCallService(voice_call_service_);
    if (!composition_root_->RequestStart([](render::ModuleLifecycleResult result) {
            if (!result) {
                LOGE("event=composition.start component=rd_application "
                     "code={} outcome=failed reason={}",
                     render::StableErrorCode(result.error().code), result.error().reason);
            }
        })) {
        init_failed_ = true;
        init_error_ = "Schedule Render composition start failed";
        return -1;
    }

    // shared_from_this() below requires this object to be created by RdApplication::Make().
    // Assign early so net_ws /ipc can late-bind OnIpcVideoFrame during module Start().
    rdApp = shared_from_this();
    module_registry_ = RenderModuleRegistry::Make(shared_from_this());
    context_->SetRenderModuleRegistry(module_registry_);

    module_registry_->StartModules();
    module_registry_->BindIngressCallbacks();
    module_registry_->DumpModuleInfo();

    // Game-hook first frames can arrive while the target process is still
    // bringing up its D3D device.  On some NVIDIA drivers, creating our
    // first device at exactly that moment may block indefinitely.  Create
    // and cache the default hardware device before starting/injecting the
    // game; GenerateD3DDevice resolves it to its actual adapter LUID.
    if (!GenerateD3DDevice(static_cast<uint64_t>(-1))) {
        LOGW("Early D3D11 device prewarm failed; will retry for the frame adapter.");
    }

    statistics_->SetApplication(shared_from_this());
    statistics_->StartMonitor();

    // connect to service
    LOGI("Will connect the service!");
    service_client_ = std::make_shared<RenderServiceClient>(shared_from_this());

    // connect panel
    LOGI("Will connect the panel!");
    ws_panel_client_ = std::make_shared<WsPanelClient>(context_);
    ws_panel_client_->Start();

    // app manager
    app_manager_ = AppManagerFactory::Make(context_);
    // encoder in thread
    encoder_thread_ = EncoderThread::Make(shared_from_this());
    // event bus listener
    msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
    state_msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
    // app shared info
    app_shared_info_ = AppSharedInfo::Make(context_);

    // app timer
    InitAppTimer();
    // messages
    InitMessages();
    // Register the application listeners before the service connection can
    // publish MsgRenderConnected2Service.  The local service may accept the
    // socket immediately; starting it earlier could lose that edge-triggered
    // notification and skip restoration of persisted virtual-display state.
    service_client_->Start();
    // global audio capture
    if (settings_->capture_.enable_audio_) {
        InitAudioCapture();
    }

    // vigem control thread
    // control_thread_ = Thread::Make("control", 16);
    // control_thread_->Poll();
    // desktop capture
    if (settings_->capture_.IsVideoInnerCapture()) {
        LOGI("Use inner capture.");
    } else {
        dda_capture_source_ = module_registry_->GetDdaCapture();
        gdi_capture_source_ = module_registry_->GetGdiCapture();
        if (dda_capture_source_) {
            capture_source_ = dda_capture_source_;
        } else if (gdi_capture_source_) {
            capture_source_ = gdi_capture_source_;
        } else {
            LOGE("Don't have a valid capture module, will exit!");
            init_failed_ = true;
            init_error_ = "no valid capture module";
            Exit();
            return -1;
        }

        // test only gdi begin
        // capture_source_ = gdi_capture_source_;
        // test only gdi end

        LOGI("Use capture fps: {}", settings_->encoder_.fps_);
        if (capture_source_ && capture_source_->IsEnabled()) {
            LOGI("Use dda capture module.");
            capture_source_->SetCaptureFps(settings_->encoder_.fps_);
            const auto weak_self = weak_from_this();
            capture_source_->SetCaptureErrorCallback([weak_self](const MonitorCaptureError& err) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                LOGE("*** capture error: {}", (int)err);
                // the callback runs on the capture thread, switching capture must be
                // done on the main thread, otherwise stopping DDA would join itself.
                self->PostGlobalTask([weak_self]() {
                    const auto self = weak_self.lock();
                    if (!self || self->exit_app_) {
                        return;
                    }
                    if (self->IsCurrentGdiCapture()) {
                        LOGI("Already use GDI capture, ignore the error.");
                        return;
                    }
                    if (self->monitor_changed_) {
                        LOGI("Maybe montor changed, ignore this error now.");
                        return;
                    }
                    // change to GDI
                    // capture_source_->SetEnabled(false);
                    LOGI("Don't use DDA, will switch to GDI.");
                    if (!self->SwitchGdiCapture() || !self->capture_source_) {
                        LOGE("Switch to GDI failed or no capture module available.");
                        return;
                    }
                    self->capture_source_->StartCapturing();
                });
            });
        } else {
            LOGI("Don't use DDA, will switch to GDI.");
            SwitchGdiCapture();
        }
    }

    if (settings_->capture_.enable_video_) {
        // application.mode in settings.toml decides path:
        // game-hook → start/inject game; desktop → screen capture (never launch game-path).
        if (settings_->IsWebViewMode()) {
            StartWebView();
        } else if (settings_->IsGameHookMode()) {
            StartProcessWithHook();
        } else {
            StartProcessWithScreenCapture();
        }
    }

    if (init_failed_) {
        LOGE("RdApplication abort after game-hook start failure: {}", init_error_);
        Exit();
        return -1;
    }

    // desktop manager
    desktop_mgr_ = WinDesktopManager::Make(context_);

    main_thread_id_ = GetCurrentThreadId();

    MSG msg{};
    while (!exit_app_) {
        BOOL ret = GetMessage(&msg, NULL, 0, 0);
        if (ret == 0 || ret == -1) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Execute pending UI tasks from RdContext
        if (context_) {
            context_->ExecutePendingUITasks();
        }

        // Execute pending global tasks
        std::queue<std::shared_ptr<AppMessage>> local;
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            local.swap(pending_tasks_);
        }
        while (!local.empty()) {
            auto& m = local.front();
            if (m->task_) {
                m->task_();
            }
            local.pop();
        }
    }
    Exit();
    return 0;
}

void RdApplication::InitAppTimer() {
    app_timer_ = std::make_shared<AppTimer>(context_);
    app_timer_->StartTimers();
}

void RdApplication::InitMessages() {
    auto weak_self = weak_from_this();
    msg_listener_->Listen<MsgBeforeInject>([weak_self](const MsgBeforeInject& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        // Prefer PrepareGameHookBoot() called synchronously before InjectDll.
        // This async path is a fallback only.
        if (self->settings_->capture_.IsVideoInnerCapture()) {
            self->PrepareGameHookBoot(msg.pid_);
        }
    });

    msg_listener_->Listen<MsgObsInjected>([weak_self](const MsgObsInjected& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        // Game-hook audio: start/restart host capture as PID process-loopback (never device mix).
        if (!self->settings_->capture_.IsVideoInnerCapture() || msg.pid_ == 0) {
            return;
        }
        if (!PreferProcessLoopbackCapture()) {
            LOGI("MsgObsInjected pid={}: skip host PID loopback (force_hook={} os_supported={})", msg.pid_, ForceInProcessHookAudio(),
                 IsProcessLoopbackCaptureSupported());
            return;
        }
        // MUST NOT run MiniAudio/WASAPI ActivateAudioInterfaceAsync on the UI/message
        // thread: the async activation needs a pumping thread and will stall ~20s then
        // fail, producing no CaptureAudioFrame (video still works on other threads).
        self->PostGlobalTask([weak_self, pid = msg.pid_]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_ || !self->audio_capture_source_) {
                LOGE("MsgObsInjected: cannot start PID audio (app/source missing) pid={}", pid);
                return;
            }
            LOGI("MsgObsInjected: schedule PID process-loopback on worker pid={}", pid);
            if (self->audio_capture_thread_ && self->audio_capture_thread_->IsJoinable()) {
                LOGI("MsgObsInjected: stopping previous audio worker before restart");
                self->audio_capture_source_->StopProviding();
                self->audio_capture_thread_->Join();
            }
            self->audio_capture_source_->SetLoopbackProcessId(pid);
            self->audio_capture_thread_ = Thread::MakeOnceTask(
                [weak_self, pid]() {
                    auto self = weak_self.lock();
                    if (!self || self->exit_app_ || !self->audio_capture_source_) {
                        return;
                    }
                    // MiniAudio manages COM itself: ma_context_init CoInitializeEx's the
                    // calling thread and ma_context_uninit balances it, and its WASAPI
                    // worker thread CoInitializeEx/CoUninitialize's itself (miniaudio.h).
                    // ProcessLoopbackAudioCapture also initializes COM on its own capture
                    // thread. So this thread's COM init is only for the duration of
                    // Stop/StartProviding and must be paired before the thread exits.
                    const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    const bool co_init = (co_hr == S_OK || co_hr == S_FALSE);
                    LOGI("PID audio worker: Stop+Start begin pid={} CoInitializeEx=0x{:08x}", pid, static_cast<unsigned>(co_hr));
                    self->audio_capture_source_->StopProviding();
                    self->audio_capture_source_->SetLoopbackProcessId(pid);
                    self->audio_capture_source_->StartProviding();
                    const bool ok = self->audio_capture_source_->IsProviding();
                    const int err = self->audio_capture_source_->GetLastStartError();
                    if (ok) {
                        LOGI("PID audio worker: StartProviding OK pid={}", pid);
                    } else {
                        LOGE("PID audio worker: StartProviding FAILED pid={} err={} "
                             "(no host capture; in-process hook disabled when loopback supported)",
                             pid, err);
                    }
                    if (co_init) {
                        CoUninitialize();
                    }
                },
                "pid audio capture", false);
        });
    });

    state_msg_listener_->Listen<MsgTimer16>([weak_self](const MsgTimer16&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->context_->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // notify dda capture
            auto module = self->module_registry_->GetDdaCapture();
            if (!module) {
                return;
            }
            module->Tick16Milliseconds();
            if (++self->timer_count_16ms_ % 2 == 0) {
                module->Tick33Milliseconds();
                timer_fps.Tick();
            }
        });

        self->PostGlobalTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->SendAudioSpectrumMessage();
        });
    });

    state_msg_listener_->Listen<MsgTimer100>([weak_self](const MsgTimer100&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->PostGlobalTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // If you want a much smoother spectrum, report it quicker, post it in MsgTimer16 callback
            self->ReportAudioSpectrum2Panel();
        });
    });

    state_msg_listener_->Listen<MsgTimer1000>([weak_self](const MsgTimer1000&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->statistics_->IncreaseRunningTime();

        if (self->media_recorder_sink_) {
            self->media_recorder_sink_->ReportPerformance();
        }
        if (self->live_pusher_sink_) {
            self->live_pusher_sink_->ReportPerformance();
        }
        if (self->pipeline_statistics_observer_) {
            self->pipeline_statistics_observer_->ReportPerformance();
        }

        auto module_registry = self->context_->GetRenderModuleRegistry();
        module_registry->On1Second();

#if MEMORY_STST_ON
        self->context_->PostTask([]() {
            auto info = MemoryStat::Instance().GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
#endif
    });

    msg_listener_->Listen<MsgClientConnected>([weak_self](const MsgClientConnected& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        // A reconnect during the grace window invalidates any shutdown
        // scheduled by the previous "last client disconnected" event.
        // Only connections with a stable id participate in game lifetime.
        // The transport name is deliberately not filtered: current web
        // clients may negotiate Direct, UDP or another registered net
        // module while preserving the same connect/disconnect id.
        const bool tracked_game_client = self->settings_->IsGameHookMode() && !msg.connection_id_.empty();
        if (tracked_game_client) {
            self->game_hook_has_seen_client_ = true;
            std::lock_guard<std::mutex> lock(self->game_hook_clients_mutex_);
            self->game_hook_client_ids_.insert(msg.connection_id_);
        }
        ++self->client_disconnect_generation_;
        if (self->settings_->IsWebViewMode() && self->webview_runtime_) {
            self->webview_runtime_->SetActive(true);
            self->webview_runtime_->SendFocusEvent(true);
        }
    });

    msg_listener_->Listen<MsgClientHello>([weak_self](const MsgClientHello&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->PostGlobalTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            // send configuration back to client
            self->SendConfigurationBack();
        });
    });

    msg_listener_->Listen<MsgClientDisconnected>([weak_self](const MsgClientDisconnected& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        if (self->settings_->IsWebViewMode()) {
            const auto generation = ++self->client_disconnect_generation_;
            const auto weak_webview = weak_self;
            self->context_->PostDelayTask(
                [weak_webview, generation]() {
                    const auto self = weak_webview.lock();
                    if (!self || self->exit_app_ || self->client_disconnect_generation_ != generation || self->HasConnectedPeer() ||
                        !self->webview_runtime_) {
                        return;
                    }
                    self->webview_runtime_->SendFocusEvent(false);
                    self->webview_runtime_->SetActive(false);
                },
                100);
            return;
        }
        if (!self->settings_->IsGameHookMode()) {
            return;
        }
        bool removed_tracked_client = false;
        if (!msg.connection_id_.empty()) {
            std::lock_guard<std::mutex> lock(self->game_hook_clients_mutex_);
            removed_tracked_client = self->game_hook_client_ids_.erase(msg.connection_id_) > 0;
        }
        // Still update the tracked set during startup so short-lived setup
        // sockets cannot keep the process alive.  Only the stop decision
        // is suppressed until the embedded web listener is ready.
        if (!self->game_hook_startup_grace_complete_) {
            LOGI("Ignore game-hook client-disconnect during startup grace period.");
            return;
        }
        if (!removed_tracked_client) {
            LOGI("Ignore untracked game-hook client-disconnect event.");
            return;
        }
        if (self->HasConnectedPeer()) {
            LOGI("Still has connected clients");
            return;
        }
        if (!self->game_hook_has_seen_client_) {
            LOGW("Ignore game-hook client-disconnect before the first confirmed client connection.");
            return;
        }

        const auto generation = ++self->client_disconnect_generation_;
        LOGI("Last game-hook client disconnected; stop render in 5 seconds unless a client reconnects.");
        self->context_->PostDelayTask(
            [weak_self, generation]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_ || self->client_disconnect_generation_ != generation) {
                    return;
                }
                if (self->HasConnectedPeer()) {
                    return;
                }
                LOGI("Game-hook grace period elapsed with no clients; stopping render.");
                ProcessUtil::KillProcess(GetCurrentProcessId());
            },
            5000);
    });

    msg_listener_->Listen<ClipboardMessage>([weak_self](const ClipboardMessage& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->PostGlobalTask([weak_self, msg]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->SendClipboardMessage(msg.msg_);
        });
    });

    // DDA Init failed
    msg_listener_->Listen<CaptureInitFailedMessage>([weak_self](const CaptureInitFailedMessage&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->PostGlobalTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->statistics_->IncreaseDDAFailedCount();
            // tell UI process to restart me
            self->RequestRestartMe();
        });
    });

    // CaptureMonitorInfoMessage
    msg_listener_->Listen<CaptureMonitorInfoMessage>([weak_self](const CaptureMonitorInfoMessage&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->PostGlobalTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            self->SendConfigurationBack();
            if (self->settings_->virtual_display_enabled_ && !self->settings_->IsGameHookMode()) {
                // The display driver may publish the Windows topology
                // notification before the Service operation response is
                // delivered. Query the authoritative ownership state after
                // that mutation has had a chance to commit, so a lost
                // response cannot leave connected clients on a stale count.
                self->context_->PostDelayTask(
                    [weak_self]() {
                        if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                            self->RefreshVirtualDisplayStatus("render-monitor-query");
                        }
                    },
                    500);
            }
        });
    });

    msg_listener_->Listen<MsgVirtualDisplayServiceResult>([weak_self](const MsgVirtualDisplayServiceResult& msg) {
        if (const auto self = weak_self.lock(); self && !self->exit_app_) {
            self->UpdateVirtualDisplayStatus(msg);
        }
    });

    msg_listener_->Listen<MsgRenderConnected2Service>([weak_self](const MsgRenderConnected2Service&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_ || !self->settings_->virtual_display_enabled_ || self->settings_->IsGameHookMode()) {
            return;
        }
        self->RefreshVirtualDisplayStatus("render-startup-query");
    });

    msg_listener_->Listen<MsgReCreateRefresher>([weak_self](const MsgReCreateRefresher&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        // report to Panel
        // !! USELESS !! Just report it
        if (self->ws_panel_client_) {
            self->ws_panel_client_->ReportMonitorChanged();
        }

        self->monitor_changed_ = true;
        self->context_->PostDelayTask(
            [weak_self]() {
                auto self = weak_self.lock();
                if (!self || self->exit_app_) {
                    return;
                }
                self->monitor_changed_ = false;
            },
            5000);
    });

    msg_listener_->Listen<MsgModifyFps>([weak_self](const MsgModifyFps& msg) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        std::lock_guard<std::mutex> lk(self->capture_source_mtx_);
        if (self->capture_source_) {
            self->settings_->encoder_.fps_ = msg.fps_;
            self->capture_source_->SetCaptureFps(msg.fps_);
        }
    });

    // request from Remote Panel's context menu or same function
    msg_listener_->Listen<MsgPanelStreamLockScreen>([](const MsgPanelStreamLockScreen& msg) {
        LOGI(" ** Panel request LockScreen from device: {}", msg.from_device_);
        Hardware::LockScreen();
    });

    // request from Remote Panel's context menu or same function
    msg_listener_->Listen<MsgPanelStreamRestartDevice>([](const MsgPanelStreamRestartDevice& msg) {
        LOGI(" ** Panel request RestartDevice from device: {}", msg.from_device_);
        Hardware::RestartDevice();
    });

    // request from Remote Panel's context menu or same function
    msg_listener_->Listen<MsgPanelStreamShutdownDevice>([](const MsgPanelStreamShutdownDevice& msg) {
        LOGI(" ** Panel request ShutdownDevice from device: {}", msg.from_device_);
        Hardware::ShutdownDevice();
    });

    state_msg_listener_->Listen<MsgTimer20S>([weak_self](const MsgTimer20S&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->context_->PostTask([weak_self]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            if (self->IsCurrentGdiCapture() && !self->force_gdi_) {
                if (auto r = self->TryInitDdaCapture(); !r) {
                    LOGI("===> Try init dda capture result failed!");
                    return;
                }
                LOGI("Will switch to DDA");
                if (auto r = self->SwitchDdaCapture(); r && self->IsCurrentDdaCapture()) {
                    LOGI("Will start DDA capturing");
                    self->capture_source_->StartCapturing();
                }
            }
        });
    });

    // Legacy desktop render watchdog.  A game-hook render owns a launched
    // game in a kill-on-close job, so using "no connected WebRTC client" as
    // a reason to restart it also terminates the game.  Game instances are
    // now lifecycle-managed by Console; without viewers they simply stop
    // capture/encode through the existing HasConnectedPeer gates.
    state_msg_listener_->Listen<MsgTimer1Minute>([weak_self](const MsgTimer1Minute&) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        if (self->settings_->IsGameHookMode()) {
            return;
        }
        ++self->restart_counter_;
        if (self->restart_counter_ >= 60 * 6) {
            self->restart_counter_ = 0;

            if (self->HasConnectedPeer()) {
                return;
            }
            LOGW("** Don't have connected clients, will restart render now.");
            ProcessUtil::KillProcess(GetCurrentProcessId());
        }
    });
}

void RdApplication::OnCapturedAudioFrame(const CaptureAudioFrame& frame) {
    auto weak_self = weak_from_this();
    if (exit_app_ || !encoded_media_bus_) {
        return;
    }

    if (!HasConnectedPeer()) {
        static thread_local uint64_t s_drop = 0;
        if (++s_drop == 1 || (s_drop % 500) == 0) {
            LOGW("CaptureAudioFrame: no connected peer, drop n={} idx={}", s_drop, frame.frame_index_);
        }
        return;
    }

    int samples = (int)frame.samples_;
    int channels = (int)frame.channels_;
    int bits = (int)frame.bits_;

    if (frame.full_data_) {
        static thread_local uint64_t s_enc = 0;
        if (++s_enc == 1 || (s_enc % 200) == 0) {
            LOGI("CaptureAudioFrame→encode: n={} {}Hz {}ch {}bit bytes={}", s_enc, samples, channels, bits, frame.full_data_->Size());
        }
        const auto captured = std::make_shared<const render::CapturedAudioFrame>(render::CapturedAudioFrame{
            .timestamp_us = static_cast<std::uint64_t>(TimeUtil::GetCurrentTimestamp()) * 1000U,
            .sample_rate_hz = static_cast<std::uint32_t>(samples),
            .channels = static_cast<std::uint16_t>(channels),
            .bits_per_sample = static_cast<std::uint16_t>(bits),
            .payload = render::MakeImmutableByteBuffer(frame.full_data_->AsString()),
        });
        const auto result = captured_media_pipeline_->HasAudioProcessors() ? captured_media_pipeline_->SubmitAudio(captured)
                                                                           : DeliverCapturedAudioFrame(captured, frame.full_data_);
        if (!result && pipeline_error_log_gate_) {
            const auto code = render::StableErrorCode(result.error().code);
            const auto decision = pipeline_error_log_gate_->Evaluate(std::string(code) + ":audio", std::chrono::steady_clock::now());
            if (decision.emit) {
                LOGW("event=pipeline.submit component=rd_application code={} "
                     "operation=audio outcome=dropped recoverable={} "
                     "suppressed={} reason={}",
                     code, result.error().recoverable, decision.suppressed_since_last_emit, result.error().reason);
            }
        }
    } else if (frame.left_ch_data_ && frame.right_ch_data_) {
        PostGlobalTask([weak_self, frame]() {
            auto self = weak_self.lock();
            if (!self || self->exit_app_) {
                return;
            }
            auto bytes = 960;
            auto single_bytes = bytes / 2;
            if (self->fft_left_.size() != single_bytes) {
                self->fft_left_.resize(single_bytes);
            }
            if (self->fft_right_.size() != single_bytes) {
                self->fft_right_.resize(single_bytes);
            }
            FFT32::DoFFT(self->fft_left_, frame.left_ch_data_, 960, true);
            FFT32::DoFFT(self->fft_right_, frame.right_ch_data_, 960, true);
            int cpy_size = 150;
            if (self->fft_left_.size() < cpy_size || self->fft_right_.size() < cpy_size) {
                return;
            }

            self->statistics_->CopyLeftSpectrum(self->fft_left_, cpy_size);
            self->statistics_->CopyRightSpectrum(self->fft_right_, cpy_size);
        });
    }
}

void RdApplication::InitAudioCapture() {
    auto weak_self = weak_from_this();
    // WebView audio is delivered by CefAudioHandler, never by the OS
    // default device or another process's loopback stream.
    if (settings_->IsWebViewMode()) {
        LOGI("WebView audio: use CEF stream callback");
        return;
    }
    if (settings_->capture_.capture_audio_type_ != Capture::CaptureAudioType::kAudioGlobal) {
        return;
    }
    if (!audio_capture_source_) {
        return;
    }

    // Desktop: start default-device loopback immediately.
    // Game-hook: wait for MsgObsInjected → PID process-loopback (never device mix).
    // If OS lacks process-loopback, rely on in-process WASAPI hook only.
    if (settings_->capture_.IsVideoInnerCapture()) {
        if (PreferProcessLoopbackCapture()) {
            LOGI("game-hook audio: defer until inject (PID process-loopback)");
        } else {
            LOGI("game-hook audio: in-process hook path "
                 "(force_hook={} os_supported={}; do not start host device-mix)",
                 ForceInProcessHookAudio(), IsProcessLoopbackCaptureSupported());
        }
    } else {
        audio_capture_thread_ = Thread::MakeOnceTask(
            [weak_self]() {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_ || !self->audio_capture_source_) {
                    return;
                }
                self->audio_capture_source_->StartProviding();
            },
            "global audio capture", false);
    }
}

void RdApplication::PostGlobalAppMessage(std::shared_ptr<AppMessage>&& msg) {
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        pending_tasks_.push(std::move(msg));
    }
    PostThreadMessage(main_thread_id_, WM_NULL, 0, 0);
}

void RdApplication::PostGlobalTask(std::function<void()>&& task) {
    PostGlobalAppMessage(AppMessageMaker::MakeTaskMessage(std::move(task)));
}

void RdApplication::PostIpcMessage(std::shared_ptr<Data>&& msg) {}

void RdApplication::PostIpcMessage(const std::string& msg) const {
    if (!settings_->capture_.IsVideoInnerCapture() || msg.empty()) {
        return;
    }
    auto data = Data::From(msg);
    // Host -> injected DLL over /ipc is a WS-specific network operation.
    module_registry_->PostWsIpcBinaryMessage(data);
}

void RdApplication::PostNetMessage(std::shared_ptr<Data> msg) const {
    if (!msg) {
        return;
    }
    module_registry_->BroadcastNetworkMessage(msg, true);
}

void RdApplication::StartProcessWithHook() {
    // Frames arrive via /ipc through the typed WS media ingress and enter
    // the same capture/encode path as desktop sources.
    if (!settings_->IsGameHookMode()) {
        LOGI("StartProcessWithHook skipped: application.mode is desktop");
        return;
    }
    // Do not let a transient setup socket terminate the game before its
    // embedded web client can connect. After this window the normal
    // tracked-client disconnect path uses the requested five-second grace.
    auto weak_self = weak_from_this();
    context_->PostDelayTask(
        [weak_self]() {
            const auto self = weak_self.lock();
            if (!self || self->exit_app_ || !self->settings_->IsGameHookMode())
                return;
            self->game_hook_startup_grace_complete_ = true;
            if (self->HasConnectedPeer())
                return;
            LOGI("Game-hook startup grace elapsed with no clients; stopping render.");
            ProcessUtil::KillProcess(GetCurrentProcessId());
            // Browser startup, Console ticket issuance and game injection can
            // overlap on a cold machine. Fifteen seconds was shorter than a real
            // cold Chromium launch and could close the listener while the first
            // page was already loading.
        },
        45000);
    LOGI("StartProcessWithHook: game_path={}, capture_method={}", settings_->app_.game_path_, (int)settings_->app_.inject_method_);
    if (settings_->app_.game_path_.empty()) {
        LOGE("StartProcessWithHook: game-path is empty, cannot start game.");
        init_failed_ = true;
        init_error_ = "game-path is empty";
        return;
    }
    bool ok = app_manager_->StartProcessWithHook();
    if (!ok) {
        LOGE("StartProcessWithHook failed for: {}", settings_->app_.game_path_);
        // Fail fast so Service can report to Console (no orphan Render without game).
        init_failed_ = true;
        init_error_ = std::format("StartProcessWithHook failed: {}", settings_->app_.game_path_);
    } else {
        LOGI("StartProcessWithHook requested OK, inject timer will attach px_gh.dll");
    }
}

void RdApplication::StartWebView() {
    if (!settings_->IsWebViewMode()) {
        return;
    }
    webview_runtime_ = std::make_unique<WebViewRuntime>();
    auto weak_self = weak_from_this();
    WebViewRuntimeConfig config{
        .url_b64 = settings_->webview_url_b64_,
        .instance_id = settings_->webview_instance_id_,
        .width = settings_->webview_width_,
        .height = settings_->webview_height_,
        .frame_rate = settings_->encoder_.fps_,
        .enable_audio = settings_->capture_.enable_audio_,
        .accelerated_paint = settings_->webview_gpu_,
    };
    WebViewRuntimeCallbacks callbacks{
        .on_video_frame =
            [weak_self](const CaptureVideoFrame& frame) {
                const auto self = weak_self.lock();
                // WebViewRuntime::SetActive is the production gate. Do not
                // re-check peer state here: room preparation and RTC data
                // channel callbacks are asynchronous, and the first repaint
                // used to be discarded in that short transition, leaving a
                // static page with no later paint and therefore a black RTC
                // track forever.
                if (!self || self->exit_app_)
                    return;
                self->encoder_thread_->Encode(frame);
            },
        .on_audio_frame =
            [weak_self](const CaptureAudioFrame& frame) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_)
                    return;
                self->OnCapturedAudioFrame(frame);
            },
        .on_cursor =
            [weak_self](const CaptureCursorBitmap& cursor) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_)
                    return;
                self->PostNetMessage(NetMessageMaker::MakeCursorInfoSyncMsg(cursor.x_, cursor.y_, cursor.hotspot_x_, cursor.hotspot_y_, cursor.width_,
                                                                            cursor.height_, cursor.visible_, cursor.data_, cursor.type_));
            },
        .on_failed =
            [weak_self](const std::string& error) {
                const auto self = weak_self.lock();
                if (!self || self->exit_app_)
                    return;
                LOGE("WebView runtime failure: {}", error);
                if (self->service_client_) {
                    self->service_client_->NotifyAppInstanceReady(self->settings_->webview_instance_id_,
                                                                  self->settings_->transmission_.listening_port_, false, error);
                }
            },
        .on_first_frame =
            [weak_self]() {
                LOGI("WebView first off-screen frame is ready");
                if (const auto self = weak_self.lock(); self && self->service_client_) {
                    self->service_client_->NotifyAppInstanceReady(self->settings_->webview_instance_id_,
                                                                  self->settings_->transmission_.listening_port_, true, "");
                }
                if (const auto self = weak_self.lock();
                    self && self->webview_runtime_ && !self->HasConnectedPeer() && !self->settings_->webview_smoke_test_) {
                    self->webview_runtime_->SetActive(false);
                }
            },
    };
    std::string error;
    if (!webview_runtime_->Start(GetModuleHandleW(nullptr), config, std::move(callbacks), error)) {
        init_failed_ = true;
        init_error_ = error.empty() ? "WebView runtime start failed" : error;
        LOGE("StartWebView failed: {}", init_error_);
        if (service_client_) {
            service_client_->NotifyAppInstanceReady(settings_->webview_instance_id_, settings_->transmission_.listening_port_, false, init_error_);
        }
        webview_runtime_.reset();
        return;
    }
    // Keep CEF nearly idle until the first viewer. Frames and audio are
    // additionally gated by HasConnectedPeer before encoding.
    // Render one probe frame so Service can distinguish BrowserReady from
    // merely finding the root process. The first-frame callback returns
    // CEF to 1 fps/inactive when no viewer is attached.
    webview_runtime_->SetActive(true);
}

void RdApplication::SendWebViewMouseEvent(const MouseEvent& event) {
    if (webview_runtime_)
        webview_runtime_->SendMouseEvent(event);
}

void RdApplication::SendWebViewKeyEvent(const KeyEvent& event) {
    if (webview_runtime_)
        webview_runtime_->SendKeyEvent(event);
}

void RdApplication::SendWebViewTextInput(const TextInput& event) {
    if (webview_runtime_)
        webview_runtime_->SendTextInput(event);
}

void RdApplication::SendWebViewFocusEvent(bool focused) {
    if (webview_runtime_)
        webview_runtime_->SendFocusEvent(focused);
}

void RdApplication::StartProcessWithScreenCapture() {
    if (capture_source_) {
        LOGI("Will start capturing by using: {}", capture_source_->Name());
        auto r = capture_source_->StartCapturing();
        if (!r) {
            LOGE("StartCapturing failed in : {}", capture_source_->Name());
            if (capture_source_->Id() == kDdaCaptureSourceId) {
                LOGW("The failed capture is DDA, will change to GDI");
                if (SwitchGdiCapture() && capture_source_) {
                    capture_source_->StartCapturing();
                }
            }
        }
    }
    app_manager_->StartProcess();
}

void RdApplication::OnCapturedVideoFrame(const CaptureVideoFrame& frame) const {
    if (captured_media_pipeline_ && captured_media_pipeline_->HasVideoProcessors() && frame.raw_image_ && frame.raw_image_->data) {
        auto pixel_format = render::VideoPixelFormat::kBgra8;
        switch (frame.raw_image_->raw_img_type_) {
        case RawImageType::kRGB:
            pixel_format = render::VideoPixelFormat::kRgb8;
            break;
        case RawImageType::kBGR:
            pixel_format = render::VideoPixelFormat::kBgr8;
            break;
        case RawImageType::kRGBA:
            pixel_format = render::VideoPixelFormat::kRgba8;
            break;
        case RawImageType::kBGRA:
            pixel_format = render::VideoPixelFormat::kBgra8;
            break;
        case RawImageType::kI420:
            pixel_format = render::VideoPixelFormat::kI420;
            break;
        case RawImageType::kI444:
            pixel_format = render::VideoPixelFormat::kI444;
            break;
        }
        std::string monitor_id;
        monitor_id.reserve(sizeof(frame.display_name_));
        for (const auto value : frame.display_name_) {
            if (value == '\0') {
                break;
            }
            monitor_id.push_back(value);
        }
        auto captured = render::CapturedVideoFrame::Create(
            render::FrameIdentity{
                .stream_id = "render-capture",
                .monitor_id = std::move(monitor_id),
                .frame_index = frame.frame_index_,
                .timestamp_us = static_cast<std::uint64_t>(TimeUtil::GetCurrentTimestamp()) * 1000U,
            },
            frame.frame_width_, frame.frame_height_, pixel_format, render::MakeImmutableByteBuffer(frame.raw_image_->data->AsString()));
        if (captured) {
            const auto result = captured_media_pipeline_->SubmitVideo(std::make_shared<const render::CapturedVideoFrame>(std::move(*captured)));
            if (!result && pipeline_error_log_gate_) {
                const auto code = render::StableErrorCode(result.error().code);
                const auto decision = pipeline_error_log_gate_->Evaluate(std::string(code) + ":video", std::chrono::steady_clock::now());
                if (decision.emit) {
                    LOGW("event=pipeline.submit component=rd_application code={} "
                         "operation=video outcome=dropped recoverable={} "
                         "suppressed={} reason={}",
                         code, result.error().recoverable, decision.suppressed_since_last_emit, result.error().reason);
                }
            }
            return;
        }
    }
    DeliverCapturedVideoFrame(frame);
}

void RdApplication::DeliverCapturedVideoFrame(const CaptureVideoFrame& frame) const {
    if (exit_app_) {
        return;
    }
    if (settings_->IsGameHookMode()) {
        std::lock_guard<std::mutex> lock(latest_game_hook_frame_mutex_);
        latest_game_hook_frame_ = frame;
        latest_game_hook_replay_frame_index_ = frame.frame_index_;
    }
    if (app_manager_) {
        app_manager_->OnCapturedVideoFrame();
    }
    if (!HasConnectedPeer()) {
        return;
    }
    if (!settings_->IsGameHookMode()) {
        if (!module_registry_->HasWorkingVideoClient()) {
            LOGI("Only audio clients, ignore video frame.");
            return;
        }
    }
    encoder_thread_->Encode(frame);
}

std::shared_ptr<render::MediaSourcePort> RdApplication::CreateMediaSourcePort() const {
    return captured_media_pipeline_ ? captured_media_pipeline_->CreateSourcePort() : std::shared_ptr<render::MediaSourcePort>{};
}

render::MediaSubmitResult RdApplication::DeliverExtensionVideoFrame(const std::shared_ptr<const render::CapturedVideoFrame>& frame) const {
    if (!frame || !frame->Payload()) {
        return std::unexpected(render::RenderError{
            .code = render::RenderErrorCode::kPipelineInvalidFrame,
            .component = "rd_application",
            .operation = "deliver_extension_video",
            .stage = "capture_output",
            .reason = "video frame or payload is missing",
            .recoverable = true,
        });
    }
    RawImageType raw_type = RawImageType::kBGRA;
    switch (frame->Format()) {
    case render::VideoPixelFormat::kRgb8:
        raw_type = RawImageType::kRGB;
        break;
    case render::VideoPixelFormat::kBgr8:
        raw_type = RawImageType::kBGR;
        break;
    case render::VideoPixelFormat::kRgba8:
        raw_type = RawImageType::kRGBA;
        break;
    case render::VideoPixelFormat::kBgra8:
        raw_type = RawImageType::kBGRA;
        break;
    case render::VideoPixelFormat::kI420:
        raw_type = RawImageType::kI420;
        break;
    case render::VideoPixelFormat::kI444:
        raw_type = RawImageType::kI444;
        break;
    case render::VideoPixelFormat::kNv12:
        return std::unexpected(render::RenderError{
            .code = render::RenderErrorCode::kPipelineInvalidFrame,
            .component = "rd_application",
            .operation = "deliver_extension_video",
            .stage = "capture_output",
            .reason = "NV12 source output is not accepted by the CPU encoder ingress",
            .recoverable = true,
        });
    }
    const auto data = Data::From(render::ImmutableByteBufferAsString(frame->Payload()));
    const auto image = Image::Make(data, static_cast<int>(frame->Width()), static_cast<int>(frame->Height()), raw_type);
    if (!image) {
        return std::unexpected(render::RenderError{
            .code = render::RenderErrorCode::kPipelineInvalidFrame,
            .component = "rd_application",
            .operation = "deliver_extension_video",
            .stage = "capture_output",
            .reason = "failed to construct encoder image",
            .recoverable = true,
        });
    }
    CaptureVideoFrame output;
    output.capture_type_ = kCaptureVideoByBitmapData;
    output.frame_width_ = frame->Width();
    output.frame_height_ = frame->Height();
    output.frame_index_ = frame->Identity().frame_index;
    output.frame_format_ = static_cast<std::uint64_t>(raw_type);
    output.adapter_uid_ = -1;
    output.raw_image_ = image;
    const auto& monitor_id = frame->Identity().monitor_id;
    const auto monitor_length = std::min(monitor_id.size(), sizeof(output.display_name_) - 1);
    std::copy_n(monitor_id.begin(), monitor_length, std::begin(output.display_name_));
    if (encoded_media_bus_) {
        encoded_media_bus_->PublishCapturedVideo(frame);
    }
    DeliverCapturedVideoFrame(output);
    return {};
}

render::MediaSubmitResult RdApplication::DeliverCapturedAudioFrame(const std::shared_ptr<const render::CapturedAudioFrame>& frame,
                                                                   const std::shared_ptr<Data>& source_data) {
    if (!frame || !frame->payload) {
        return std::unexpected(render::RenderError{
            .code = render::RenderErrorCode::kPipelineInvalidFrame,
            .component = "rd_application",
            .operation = "deliver_captured_audio",
            .stage = "capture_output",
            .reason = "audio frame or payload is missing",
            .recoverable = true,
        });
    }
    encoded_media_bus_->PublishCapturedAudio(frame);
    const auto samples = static_cast<int>(frame->sample_rate_hz);
    const auto channels = static_cast<int>(frame->channels);
    const auto bits = static_cast<int>(frame->bits_per_sample);
    const auto data = source_data ? source_data : Data::From(render::ImmutableByteBufferAsString(frame->payload));
    auto stat = RdStatistics::Instance();
    stat->audio_samples_ = samples;
    stat->audio_channels_ = channels;
    stat->audio_bits_ = bits;
    const auto weak_self = weak_from_this();
    context_->PostMediaTask([weak_self, data, samples, channels, bits] {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_ || !self->module_registry_) {
            return;
        }
        self->module_registry_->BroadcastRawAudio(data, samples, channels, bits);
    });
    const auto current_time = TimeUtil::GetCurrentTimestamp();
    if (last_post_audio_time_ == 0) {
        last_post_audio_time_ = current_time;
    }
    const auto diff = current_time - last_post_audio_time_;
    last_post_audio_time_ = current_time;
    statistics_->AppendAudioFrameGap(diff);
    return {};
}

void RdApplication::ReplayLatestGameHookFrame() const {
    if (exit_app_ || !settings_->IsGameHookMode() || !HasConnectedPeer()) {
        return;
    }
    std::optional<CaptureVideoFrame> frame;
    {
        std::lock_guard<std::mutex> lock(latest_game_hook_frame_mutex_);
        frame = latest_game_hook_frame_;
        if (frame) {
            frame->frame_index_ = ++latest_game_hook_replay_frame_index_;
        }
    }
    if (!frame) {
        LOGI("Game-hook viewer connected before the first captured frame");
        return;
    }
    LOGI("Replay cached game-hook frame on viewer connect: index={} size={}x{}", frame->frame_index_, frame->frame_width_, frame->frame_height_);
    // The encoder may not have existed when ProcessClientConnectedEvent
    // requested an IDR. Carry the request on the frame itself so a newly
    // created encoder also produces a decodable first packet.
    frame->request_idr_ = true;
    encoder_thread_->Encode(*frame);
}

void RdApplication::OnCapturedCursorBitmap(const CaptureCursorBitmap& cursor) const {
    if (exit_app_) {
        return;
    }
    PostNetMessage(NetMessageMaker::MakeCursorInfoSyncMsg(cursor.x_, cursor.y_, cursor.hotspot_x_, cursor.hotspot_y_, cursor.width_, cursor.height_,
                                                          cursor.visible_, cursor.data_, cursor.type_));
}

void RdApplication::OnIpcVideoFrame(const std::shared_ptr<CaptureVideoFrame>& msg) const {
    if (!HasConnectedPeer()) {
        return;
    }
    OnCapturedVideoFrame(*msg);
}

void RdApplication::OnIpcAudioFrame(const CaptureAudioFrame& frame) {
    // In-process media path: do not enqueue high-rate PCM on the application bus.
    if (!context_) {
        LOGE("OnIpcAudioFrame: context_ null, drop frame idx={} pcm={}", frame.frame_index_, frame.full_data_ ? frame.full_data_->Size() : 0);
        return;
    }
    if (!frame.full_data_ || frame.full_data_->Size() <= 0) {
        LOGE("OnIpcAudioFrame: empty pcm idx={} {}Hz {}ch", frame.frame_index_, frame.samples_, frame.channels_);
        return;
    }
    static thread_local uint64_t s_n = 0;
    if (++s_n == 1 || (s_n % 200) == 0) {
        LOGI("OnIpcAudioFrame: n={} idx={} {}Hz {}ch {}bit bytes={} → media", s_n, frame.frame_index_, frame.samples_, frame.channels_, frame.bits_,
             frame.full_data_->Size());
    }
    OnCapturedAudioFrame(frame);
}

bool RdApplication::HasConnectedPeer() const {
    if (module_registry_->GetTotalMediaConsumersCount()) {
        return true;
    }
    if (!settings_->IsGameHookMode()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(game_hook_clients_mutex_);
    return !game_hook_client_ids_.empty();
}

void RdApplication::WriteBoostUpInfoForPid(uint32_t pid) {
    PrepareGameHookBoot(pid);
}

void RdApplication::PrepareGameHookBoot(uint32_t pid) {
    if (!app_shared_message_) {
        LOGE("PrepareGameHookBoot: no AppSharedMessage (offsets/port)");
        return;
    }
    if (!app_shared_info_) {
        LOGE("PrepareGameHookBoot: no AppSharedInfo writer");
        return;
    }
    app_shared_message_->ipc_port_ = settings_->transmission_.listening_port_;
    app_shared_message_->self_size_ = sizeof(AppSharedMessage);
    app_shared_message_->enable_hook_events_ = 1;
    // Prefer OS process-loopback when available; otherwise (or PIXELS_FORCE_HOOK_AUDIO=1)
    // enable in-process WASAPI/XAudio2 hook.
    const bool prefer_pid = PreferProcessLoopbackCapture();
    app_shared_message_->enable_hook_audio_ = prefer_pid ? 0u : 1u;
    LOGI("PrepareGameHookBoot pid={}: prefer_pid_loopback={}, force_hook={}, "
         "os_supported={}, enable_hook_audio={}",
         pid, prefer_pid, ForceInProcessHookAudio(), IsProcessLoopbackCaptureSupported(), app_shared_message_->enable_hook_audio_);

    std::string buffer;
    buffer.resize(sizeof(AppSharedMessage));
    memcpy(buffer.data(), app_shared_message_.get(), sizeof(AppSharedMessage));
    if (!app_shared_info_->WriteBootConfig(pid, buffer)) {
        LOGE("PrepareGameHookBoot failed for pid {}", pid);
        return;
    }
    // Allow this pid on /ipc (net_ws). Game restarts get here again with the new
    // pid, so each live game generation is re-registered; stale games injected by
    // dead renders are never registered and get rejected on connect.
    module_registry_->RegisterWsIpcPid(pid);
}

void RdApplication::SendAudioSpectrumMessage() const {
    auto st = RdStatistics::Instance();
    auto msg = std::make_shared<Message>();
    msg->set_type(px::kRendererAudioSpectrum);
    auto sas = msg->mutable_renderer_audio_spectrum();
    sas->set_samples(st->audio_samples_);
    sas->set_bits(st->audio_bits_);
    sas->set_channels(st->audio_channels_);
    auto left_spectrum = st->GetLeftSpectrum();
    auto right_spectrum = st->GetRightSpectrum();
    sas->mutable_left_spectrum()->Add(left_spectrum.begin(), left_spectrum.end());
    sas->mutable_right_spectrum()->Add(right_spectrum.begin(), right_spectrum.end());
    auto net_msg = ProtoAsData(msg);

    // audio spectrum
    PostNetMessage(net_msg);
}

void RdApplication::ReportAudioSpectrum2Panel() {
    auto st = RdStatistics::Instance();
    auto msg = std::make_shared<pxrp::RpMessage>();
    msg->set_type(pxrp::kRpServerAudioSpectrum);
    auto sas = msg->mutable_renderer_audio_spectrum();
    sas->set_samples(st->audio_samples_);
    sas->set_bits(st->audio_bits_);
    sas->set_channels(st->audio_channels_);
    auto left_spectrum = st->GetLeftSpectrum();
    auto right_spectrum = st->GetRightSpectrum();
    sas->mutable_left_spectrum()->Add(left_spectrum.begin(), left_spectrum.end());
    sas->mutable_right_spectrum()->Add(right_spectrum.begin(), right_spectrum.end());
    auto buffer = RpProtoAsData(msg);
    PostPanelMessage(buffer);
}

void RdApplication::SendClipboardMessage(const std::string& msg) const {
    px::Message m;
    m.set_type(px::kClipboardInfo);
    m.mutable_clipboard_info()->set_msg(msg);
    auto buffer = ProtoAsData(&m);
    PostNetMessage(buffer);
}

void RdApplication::SendConfigurationBack() {
    std::shared_ptr<MonitorCaptureSource> capture_source;
    {
        std::lock_guard<std::mutex> lk(capture_source_mtx_);
        capture_source = capture_source_;
    }

    std::vector<CaptureMonitorInfo> monitors;
    std::string capturing_name;
    const auto append_synthetic_monitor = [&monitors, &capturing_name](const std::string& name, int width, int height) {
        capturing_name = name;
        CaptureMonitorInfo monitor;
        monitor.name_ = name;
        monitor.primary_ = true;
        monitor.attached_desktop_ = true;
        monitor.right_ = width;
        monitor.bottom_ = height;
        monitor.supported_res_.push_back(SupportedResolution{
            .width_ = static_cast<unsigned long>(width),
            .height_ = static_cast<unsigned long>(height),
        });
        monitors.push_back(std::move(monitor));
    };
    if (capture_source) {
        monitors = capture_source->CaptureMonitors();
        capturing_name = capture_source->CapturingMonitorName();
        this->UpdateCapturingMonitorInfo();
    } else if (settings_->IsGameHookMode()) {
        // Inner/game-hook capture has no desktop monitor module. It is a
        // single application surface, so expose a synthetic monitor to the
        // standard client configuration/decode pipeline.
        int width = settings_->encoder_.encode_width_;
        int height = settings_->encoder_.encode_height_;
        if (app_manager_) {
            const auto hwnd = static_cast<HWND>(app_manager_->GetWindowHandle());
            RECT client_rect{};
            if (hwnd && IsWindow(hwnd) && GetClientRect(hwnd, &client_rect) && client_rect.right > client_rect.left &&
                client_rect.bottom > client_rect.top) {
                width = client_rect.right - client_rect.left;
                height = client_rect.bottom - client_rect.top;
            }
        }
        append_synthetic_monitor("Application", width, height);
        LOGI("Use synthetic game-hook monitor configuration: {}x{}", width, height);
    } else if (settings_->IsWebViewMode()) {
        append_synthetic_monitor("webview", settings_->webview_width_, settings_->webview_height_);
        LOGI("Use synthetic WebView monitor configuration: {}x{}", settings_->webview_width_, settings_->webview_height_);
    } else {
        LOGE("SendConfigurationBack failed, working monitor capture module is null.");
        return;
    }
    if (monitors.empty()) {
        LOGW("Ignore this sending configuration back, 'cause there's no monitors detected.");
        return;
    }

    px::Message m;
    m.set_type(px::kServerConfiguration);
    auto config = m.mutable_config(); // NOLINT(gammaray-raw-pointer-boundary): transient protobuf view
    // screen info
    auto monitors_info = config->mutable_monitors_info(); // NOLINT(gammaray-raw-pointer-boundary): transient protobuf view
    LOGI("Will send configuration back, monitor size: {}", monitors.size());
    for (int i = 0; i < monitors.size(); i++) {
        auto monitor = monitors[i];
        MonitorInfo info;
        info.set_name(monitor.name_);
        for (const auto& res : monitor.supported_res_) {
            MonitorResolution mr;
            mr.set_width(res.width_);
            mr.set_height(res.height_);
            info.mutable_resolutions()->Add(std::move(mr));
        }
        info.set_current_width(monitor.Width());
        info.set_current_height(monitor.Height());
        monitors_info->Add(std::move(info));
    }
    LOGI("Will send configuration back, fps: {}", settings_->encoder_.fps_);
    config->set_fps(settings_->encoder_.fps_);
    config->set_capturing_monitor_name(capturing_name);
    config->set_file_transfer_enabled(settings_->file_transfer_enabled_);
    // FT 协议版本:rustdesk 语义 = 2(旧实现已删除,主控按此门控)
    config->set_ft_protocol_version(2);
    config->set_audio_enabled(settings_->audio_enabled_);
    config->set_can_be_operated(settings_->can_be_operated_);
    config->set_virtual_display_enabled(settings_->virtual_display_enabled_ && !settings_->IsGameHookMode());
    config->set_virtual_display_owned_count(virtual_display_owned_count_.load());
    config->set_virtual_display_max_count(kVirtualDisplayMaximumCount);
    config->set_topology_generation(virtual_display_topology_generation_.load());
    config->set_voice_call_enabled(settings_->voice_call_enabled_);
    config->set_voice_call_protocol_version(settings_->voice_call_enabled_ ? 1 : 0);
    config->set_voice_call_requires_headset(true);
    //
    auto buffer = ProtoAsData(&m);
    PostNetMessage(buffer);
}

void RdApplication::RequestRestartMe() const {
    if (!ws_panel_client_) {
        LOGW("Cannot request Render restart: Panel client is disabled");
        return;
    }
    pxrp::RpMessage m;
    m.set_type(pxrp::kRpRestartServer);
    m.mutable_restart_server()->set_reason("restart");
    auto buffer = RpProtoAsData(&m);
    ws_panel_client_->PostNetMessage(buffer);
}

void RdApplication::ReportFileTransferAuditBegin(const render::FileTransferAuditBegin& audit) {
    const std::weak_ptr<RdApplication> weak_application = weak_from_this();
    PostGlobalTask([weak_application, audit] {
        const auto application = weak_application.lock();
        if (!application || application->exit_app_) {
            return;
        }
        pxrp::RpMessage message;
        message.set_type(pxrp::kRpFileTransferBegin);
        auto& begin = *message.mutable_ft_begin();
        begin.set_the_file_id(audit.file_id);
        begin.set_begin_timestamp(audit.begin_timestamp);
        begin.set_direction(audit.direction);
        begin.set_file_detail(audit.file_detail);
        begin.set_visitor_device_id(audit.visitor_device_id);
        static_cast<void>(application->PostPanelMessage(RpProtoAsData(&message)));
    });
}

void RdApplication::ReportFileTransferAuditEnd(const render::FileTransferAuditEnd& audit) {
    const std::weak_ptr<RdApplication> weak_application = weak_from_this();
    PostGlobalTask([weak_application, audit] {
        const auto application = weak_application.lock();
        if (!application || application->exit_app_) {
            return;
        }
        pxrp::RpMessage message;
        message.set_type(pxrp::kRpFileTransferEnd);
        auto& end = *message.mutable_ft_end();
        end.set_the_file_id(audit.file_id);
        end.set_end_timestamp(audit.end_timestamp);
        end.set_success(audit.success);
        end.set_status(audit.status);
        end.set_end_reason(audit.reason);
        static_cast<void>(application->PostPanelMessage(RpProtoAsData(&message)));
    });
}

void RdApplication::ResetMonitorResolution(const std::string& name, int w, int h) {
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = w;
    dm.dmPelsHeight = h;
    dm.dmBitsPerPel = 32;
    dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    auto deviceName = StringUtil::ToWString(name); // L"\\\\.\\DISPLAY1";
    LONG result = ChangeDisplaySettingsExW(deviceName.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr);
    bool ok = result == DISP_CHANGE_SUCCESSFUL;

    px::Message m;
    m.set_type(px::kChangeMonitorResolutionResult);
    auto r = m.mutable_change_monitor_resolution_result(); // NOLINT(gammaray-raw-pointer-boundary): transient protobuf view
    r->set_monitor_name(name);
    r->set_result(ok);
    auto buffer = ProtoAsData(&m);
    PostNetMessage(buffer);
}

std::shared_ptr<RenderModuleRegistry> RdApplication::GetRenderModuleRegistry() {
    return module_registry_;
}

std::shared_ptr<MonitorCaptureSource> RdApplication::GetWorkingMonitorCaptureSource() {
    std::lock_guard<std::mutex> lk(capture_source_mtx_);
    return capture_source_;
}

std::map<std::string, std::shared_ptr<VideoEncoderModule>> RdApplication::GetWorkingVideoEncoders() const {
    if (encoder_thread_) {
        return encoder_thread_->GetWorkingVideoEncoders();
    }
    return {};
}

bool RdApplication::GenerateD3DDevice(uint64_t adapter_uid) {
    LOGI("GenerateD3DDevice, adapter_uid = {}", adapter_uid);
    ClearD3DDevice(adapter_uid);
    ClearModuleD3DState(adapter_uid);

    auto new_device_wrapper = std::make_shared<D3D11DeviceWrapper>();

    ComPtr<IDXGIFactory1> factory1;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC desc;
    HRESULT res = NULL;
    int adapter_index = 0;
    bool adapter_found = false;
    res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory1.GetAddressOf()));
    if (res != S_OK) {
        LOGE("CreateDXGIFactory1 failed");
        return false;
    }
    if (adapter_uid != static_cast<uint64_t>(-1)) {
        while (true) {
            adapter.Reset();
            res = factory1->EnumAdapters1(adapter_index, adapter.GetAddressOf());
            if (res != S_OK) {
                LOGW("EnumAdapters1 index:{} failed, adapter_uid={}", adapter_index, adapter_uid);
                break;
            }

            adapter->GetDesc(&desc);
            if (adapter_uid == desc.AdapterLuid.LowPart) {
                LOGI("Adapter Index:{} Name: {}", adapter_index, StringUtil::ToUTF8(desc.Description).c_str());
                LOGI("find adapter");
                adapter_found = true;
                break;
            }
            ++adapter_index;
        }
    }

    D3D_FEATURE_LEVEL featureLevel;
    if (adapter_found) {
        LOGI("D3D11CreateDevice begin for matched adapter uid={}", adapter_uid);
        res = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
        LOGI("D3D11CreateDevice end for matched adapter uid={}, hr={}", adapter_uid, res);
    } else {
        LOGW("Adapter uid {} not found or virtual/RDP path, fallback to generic D3D device creation", adapter_uid);
        res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
        if (res != S_OK || !new_device_wrapper->d3d11_device_ || !new_device_wrapper->d3d11_device_context_) {
            LOGW("Fallback hardware D3D11CreateDevice failed: {}, try WARP", res);
            new_device_wrapper->d3d11_device_.Reset();
            new_device_wrapper->d3d11_device_context_.Reset();
            res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                    &new_device_wrapper->d3d11_device_, &featureLevel, &new_device_wrapper->d3d11_device_context_);
        }
    }

    if (res != S_OK || !new_device_wrapper->d3d11_device_ || !new_device_wrapper->d3d11_device_context_) {
        LOGE("D3D11CreateDevice failed: {}", res);
        ClearD3DDevice(adapter_uid);
        return false;
    } else {
        uint64_t device_adapter_uid = adapter_uid;
        if (adapter_uid == static_cast<uint64_t>(-1)) {
            ComPtr<IDXGIDevice> dxgi_device;
            ComPtr<IDXGIAdapter> device_adapter;
            DXGI_ADAPTER_DESC device_desc{};
            if (SUCCEEDED(new_device_wrapper->d3d11_device_.As(&dxgi_device)) && dxgi_device && SUCCEEDED(dxgi_device->GetAdapter(&device_adapter)) &&
                device_adapter && SUCCEEDED(device_adapter->GetDesc(&device_desc))) {
                device_adapter_uid = device_desc.AdapterLuid.LowPart;
                LOGI("D3D11 prewarm resolved adapter: {} (uid={})", StringUtil::ToUTF8(device_desc.Description), device_adapter_uid);
            } else {
                LOGW("D3D11 prewarm could not resolve the selected adapter LUID.");
            }
        }
        LOGI("D3D11CreateDevice mDevice = {}", (void*)new_device_wrapper->d3d11_device_.Get());
        new_device_wrapper->adapter_uid_ = device_adapter_uid;
        d3d11_devices_[device_adapter_uid] = new_device_wrapper;
        d3d11_device_failure_counts_[device_adapter_uid] = 0;
        return true;
    }
}

void RdApplication::ClearD3DDevice(uint64_t adapter_uid) {
    if (!d3d11_devices_.contains(adapter_uid)) {
        return;
    }
    if (d3d11_devices_[adapter_uid]) {
        d3d11_devices_[adapter_uid]->Release();
    }
    d3d11_devices_.erase(adapter_uid);
}

void RdApplication::ClearModuleD3DState(uint64_t adapter_uid) {
    if (!module_registry_) {
        return;
    }
    module_registry_->ClearModuleD3DResources(adapter_uid);
}

void RdApplication::HandleD3DDeviceFailure(uint64_t adapter_uid, const std::string& reason) {
    LOGE("HandleD3DDeviceFailure adapter_uid={}, reason={}", adapter_uid, reason);
    ClearD3DDevice(adapter_uid);
    ClearModuleD3DState(adapter_uid);
    if (encoder_thread_) {
        encoder_thread_->HandleD3DDeviceFailure(adapter_uid);
    }

    const auto fail_count = ++d3d11_device_failure_counts_[adapter_uid];
    if (fail_count < 2) {
        return;
    }

    auto weak_self = weak_from_this();
    context_->PostTask([weak_self, adapter_uid, fail_count]() {
        auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        if (!self->IsCurrentDdaCapture() || self->force_gdi_) {
            return;
        }
        LOGW("D3D device generation failed repeatedly, downgrade capture to GDI. adapter_uid={}, fail_count={}", adapter_uid, fail_count);
        if (self->SwitchGdiCapture() && self->capture_source_) {
            self->capture_source_->StartCapturing();
        }
    });
}

ComPtr<ID3D11Device> RdApplication::GetD3DDevice(uint64_t adapter_uid) {
    if (auto it = d3d11_devices_.find(adapter_uid); it != d3d11_devices_.end()) {
        return it->second ? it->second->d3d11_device_ : nullptr;
    }
    // GDI/raw capture has no DXGI adapter LUID and reports UINT64_MAX.
    // GenerateD3DDevice resolves a generic hardware device to its real
    // adapter LUID before caching it, so look up that resolved default
    // instead of immediately creating another device under the sentinel.
    if (adapter_uid == static_cast<uint64_t>(-1)) {
        for (const auto& [uid, wrapper] : d3d11_devices_) {
            if (wrapper && wrapper->d3d11_device_) {
                return wrapper->d3d11_device_;
            }
        }
    }
    return nullptr;
}

ComPtr<ID3D11DeviceContext> RdApplication::GetD3DContext(uint64_t adapter_uid) {
    if (auto it = d3d11_devices_.find(adapter_uid); it != d3d11_devices_.end()) {
        return it->second ? it->second->d3d11_device_context_ : nullptr;
    }
    if (adapter_uid == static_cast<uint64_t>(-1)) {
        for (const auto& [uid, wrapper] : d3d11_devices_) {
            if (wrapper && wrapper->d3d11_device_context_) {
                return wrapper->d3d11_device_context_;
            }
        }
    }
    return nullptr;
}

void RdApplication::ReqCtrlAltDelete(const std::string& device_id, const std::string& stream_id) const {
    if (!service_client_ || !service_client_->IsAlive()) {
        LOGE("Service client not connected, can't ReqCtrlAltDelete");
        return;
    }
    px::ServiceMessage m;
    m.set_type(ServiceMessageType::kSrvReqCtrlAltDelete);
    m.mutable_req_ctrl_alt_delete()->set_req_device_id(device_id);
    m.mutable_req_ctrl_alt_delete()->set_req_stream_id(stream_id);
    service_client_->PostNetMessage(m.SerializeAsString());
}

void RdApplication::RedeemConnectionTicket(
    const std::string& ticket, const std::string& client_nonce, const std::string& instance_id,
    std::function<void(bool, const std::string&, const std::vector<std::string>&, const std::string&, const std::string&, const std::string&,
                       const std::string&, const std::string&, int64_t, bool, bool)>&& callback) const {
    if (!service_client_ || !service_client_->IsAlive()) {
        callback(false, "SERVICE_UNAVAILABLE", {}, "", "", "", "", "", 0, true, true);
        return;
    }
    service_client_->RedeemConnectionTicket(ticket, client_nonce, instance_id, std::move(callback));
}

void RdApplication::RequestVirtualDisplay(const std::string& request_id, int operation, uint32_t width, uint32_t height, uint32_t refresh_hz,
                                          std::function<void(const MsgVirtualDisplayServiceResult&)>&& callback) {
    if (!service_client_ || !service_client_->IsAlive()) {
        MsgVirtualDisplayServiceResult result;
        result.request_id_ = request_id;
        result.error_code_ = "SERVICE_UNAVAILABLE";
        result.error_message_ = "px_service is unavailable";
        callback(result);
        return;
    }
    service_client_->RequestVirtualDisplay(request_id, operation, width, height, refresh_hz, std::move(callback));
}

void RdApplication::UpdateVirtualDisplayStatus(const MsgVirtualDisplayServiceResult& result) {
    if (!result.accepted_) {
        LOGW("Ignore failed virtual display status: request={}, code={}", result.request_id_, result.error_code_);
        return;
    }
    const auto current_generation = virtual_display_topology_generation_.load();
    const auto current_owned_count = virtual_display_owned_count_.load();
    if (result.topology_generation_ < current_generation) {
        LOGW("Ignore stale virtual display status: request={}, incoming={}, current={}", result.request_id_, result.topology_generation_,
             current_generation);
        return;
    }
    if (result.topology_generation_ == current_generation && result.owned_display_count_ == current_owned_count) {
        return;
    }
    virtual_display_owned_count_.store(result.owned_display_count_);
    virtual_display_topology_generation_.store(result.topology_generation_);
    LOGI("Apply virtual display status: request={}, owned={}, generation={}", result.request_id_, result.owned_display_count_,
         result.topology_generation_);
    SendConfigurationBack();
}

void RdApplication::RefreshVirtualDisplayStatus(const std::string& request_prefix) {
    if (exit_app_ || !settings_->virtual_display_enabled_ || settings_->IsGameHookMode()) {
        return;
    }
    if (virtual_display_refresh_pending_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto request_sequence = virtual_display_request_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto request_id = std::format("{}-{}-{}-{}", request_prefix, GetCurrentProcessId(), GetTickCount64(), request_sequence);
    const auto weak_self = weak_from_this();
    RequestVirtualDisplay(request_id, kVirtualDisplayQuery, 1920, 1080, 60, [weak_self](const MsgVirtualDisplayServiceResult& result) {
        const auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        self->virtual_display_refresh_pending_.store(false, std::memory_order_release);
        // Keep the callback as a topology-rebuild-safe fallback. The
        // message-bus copy can be discarded when the state lane is
        // rebuilt; UpdateVirtualDisplayStatus is idempotent for the
        // same generation/count, so callback and broadcast may race.
        self->PostGlobalTask([weak_self, result]() {
            if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                self->UpdateVirtualDisplayStatus(result);
            }
        });
    });
}

std::pair<uint32_t, uint64_t> RdApplication::GetVirtualDisplayStatusSnapshot() const {
    return {
        virtual_display_owned_count_.load(),
        virtual_display_topology_generation_.load(),
    };
}

void RdApplication::OnServiceRequestedStop() {
    LOGW("Service requested stop (Console stop instance), notify clients then exit.");
    // broadcast kInstanceStopped to all RTC clients, then leave some time
    // for the message to be flushed out before exiting by ourselves
    PostNetMessage(NetMessageMaker::MakeInstanceStopped("stopped by Console"));
    const auto weak_self = weak_from_this();
    context_->PostDelayTask(
        [weak_self]() {
            if (const auto self = weak_self.lock(); self && !self->exit_app_) {
                self->Exit();
            }
        },
        400);
}

std::shared_ptr<WinDesktopManager> RdApplication::GetDesktopManager() {
    return desktop_mgr_;
}

bool RdApplication::SwitchGdiCapture() {
    if (IsCurrentGdiCapture()) {
        return true;
    }

    std::lock_guard<std::mutex> lk(capture_source_mtx_);
    if (capture_source_) {
        capture_source_->StopCapturing();
        capture_source_->SetEnabled(false);
    }
    if (!gdi_capture_source_) {
        LOGE("Don't have gdi module, ignore!");
        return false;
    }
    capture_source_ = gdi_capture_source_;
    capture_source_->SetCaptureFps(settings_->encoder_.fps_);
    capture_source_->SetEnabled(true);
    LOGI("Use gdi capture module.");
    return true;
}

bool RdApplication::SwitchDdaCapture() {
    if (IsCurrentDdaCapture()) {
        return true;
    }

    std::lock_guard<std::mutex> lk(capture_source_mtx_);
    if (capture_source_) {
        capture_source_->StopCapturing();
        capture_source_->SetEnabled(false);
    }
    if (!dda_capture_source_) {
        LOGE("Don't have gdi module, ignore!");
        return false;
    }
    capture_source_ = dda_capture_source_;
    capture_source_->SetCaptureFps(settings_->encoder_.fps_);
    capture_source_->SetEnabled(true);
    LOGI("Use dda capture module.");
    return true;
}

bool RdApplication::IsCurrentGdiCapture() {
    std::lock_guard<std::mutex> lk(capture_source_mtx_);
    return capture_source_ && capture_source_->Id() == kGdiCaptureSourceId;
}

bool RdApplication::IsCurrentDdaCapture() {
    std::lock_guard<std::mutex> lk(capture_source_mtx_);
    return capture_source_ && capture_source_->Id() == kDdaCaptureSourceId;
}

bool RdApplication::TryInitDdaCapture() {
    if (!dda_capture_source_) {
        return false;
    }
    return dda_capture_source_->InitializeCapture();
}

bool RdApplication::PostPanelMessage(std::shared_ptr<Data> msg) {
    if (ws_panel_client_ && msg) {
        return ws_panel_client_->PostNetMessage(msg);
    }
    return false;
}

void RdApplication::PostUserProxyMessage(std::shared_ptr<Data> msg) {
    if (!msg || !module_registry_) {
        return;
    }
    module_registry_->PostWsUserProxyMessage(msg);
}

void RdApplication::HandleForceGdiEvent(bool force_gdi) {
    // WebView frames come from CEF OSR and do not have a desktop capture
    // module. Relay's legacy RequestControl hint must not try to switch a
    // nonexistent DDA/GDI source.
    if (settings_->IsWebViewMode()) {
        return;
    }
    force_gdi_ = force_gdi;
    auto weak_self = weak_from_this();
    context_->PostTask([weak_self, force_gdi]() {
        auto self = weak_self.lock();
        if (!self || self->exit_app_) {
            return;
        }
        if (force_gdi) {
            self->SwitchGdiCapture();
        } else {
            self->SwitchDdaCapture();
        }
        if (self->capture_source_) {
            self->capture_source_->StartCapturing();
        }
    });
}

void RdApplication::UpdateCapturingMonitorInfo() {
    const auto module = this->GetWorkingMonitorCaptureSource();
    if (!module) {
        LOGE("ProcessCapturingMonitorInfoEvent failed, module is null.");
        return;
    }
    const auto cm_msg = CaptureMonitorInfoMessage{.monitors_ = module->CaptureMonitors(),
                                                  .capturing_monitor_name_ = module->CapturingMonitorName(),
                                                  .virtual_desktop_bound_rectangle_info_ = module->VirtualDesktopBounds()};

    LOGI("Config Monitors size: {}", cm_msg.monitors_.size());
    if (cm_msg.monitors_.empty()) {
        LOGE("Don't have monitors, ignore the event replayer updating.");
        return;
    }

    if (input_replay_service_) {
        input_replay_service_->UpdateCaptureMonitorInfo(cm_msg);
        LOGI("Update CaptureMonitorInfo to input replay service finished.");
    }
}

void RdApplication::Exit() {
    const auto application_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
    if (application_runtime && application_runtime->IsRuntimeThread()) {
        if (exit_dispatch_pending_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto owner = weak_from_this().lock();
        if (owner && ApplicationShutdownDispatcher::Instance()->Submit(owner)) {
            LOGI("event=application.shutdown component=rd_application operation=ordered_shutdown outcome=deferred "
                 "reason=shutdown_requested_from_runtime_thread");
            return;
        }
        exit_dispatch_pending_.store(false, std::memory_order_release);
        LOGE("event=application.shutdown component=rd_application code=ASYNC_SCOPE_SPAWN_FAILED "
             "operation=ordered_shutdown outcome=failed recoverable=false reason=shutdown_dispatch_unavailable");
        return;
    }
    if (exit_app_.exchange(true)) {
        return;
    }
    const auto shutdown_started = std::chrono::steady_clock::now();
    const auto shutdown_deadline = shutdown_started + kApplicationShutdownBudget;
    // Flip the guard first so asynchronous callbacks stop touching the
    // pipeline while its owners are being released.
    if (msg_listener_) {
        msg_listener_->UnListenAll();
    }
    if (state_msg_listener_) {
        state_msg_listener_->UnListenAll();
    }
    // stop the statistics reporting at first: it runs on the context task pool
    // and reads net plugins, so it must be silent before capturing stops
    if (statistics_) {
        LOGI("RdApplication shutdown: statistics");
        statistics_->Exit();
    }
    if (app_timer_) {
        LOGI("RdApplication shutdown: timers");
        app_timer_->StopTimers();
    }
    if (module_registry_) {
        module_registry_->StopRouting();
    }
    if (ws_panel_client_ || service_client_ || module_registry_) {
        LOGI("event=application.shutdown component=rd_application operation=stop_network_clients outcome=started");
        const auto async_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
        if (async_runtime && !async_runtime->IsStopping() && !async_runtime->IsRuntimeThread()) {
            const auto shutdown_scope = PxAsyncScope::Create(async_runtime, PxAsyncLane::kControl);
            const auto completion = std::make_shared<std::promise<PxResult<void>>>();
            auto future = completion->get_future();
            const auto spawned =
                shutdown_scope &&
                shutdown_scope->Spawn("application-network-shutdown", [panel_client = ws_panel_client_, service_client = service_client_,
                                                                       module_registry = module_registry_, shutdown_deadline, completion]() {
                    return StopApplicationNetworkClients(panel_client, service_client, module_registry, shutdown_deadline, completion);
                });
            if (!spawned || future.wait_until(shutdown_deadline) != std::future_status::ready) {
                LOGE("event=application.shutdown component=rd_application code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                     "operation=stop_network_clients outcome=timeout recoverable=false");
                if (shutdown_scope) {
                    shutdown_scope->BeginStop();
                }
            } else {
                const auto stopped = future.get();
                if (!stopped) {
                    LOGE("event=application.shutdown component=rd_application code={} operation=stop_network_clients "
                         "outcome=failed recoverable={} reason={}",
                         stopped.Error().StableCode(), stopped.Error().retryable, stopped.Error().message);
                } else {
                    ws_panel_client_.reset();
                    service_client_.reset();
                    LOGI("event=application.shutdown component=rd_application operation=stop_network_clients outcome=success");
                }
            }
        } else {
            LOGI("event=application.shutdown component=rd_application operation=stop_network_clients outcome=deferred "
                 "reason=runtime_thread_or_runtime_unavailable");
            if (ws_panel_client_) {
                ws_panel_client_->Exit();
            }
            if (service_client_) {
                service_client_->Exit();
            }
        }
    }
    if (webview_runtime_) {
        webview_runtime_->Stop();
        webview_runtime_.reset();
    }
    // Stop capture producers before their concrete module owners. The former
    // plug-in lifetime workaround is no longer valid after built-in modules
    // moved to shared RAII ownership.
    {
        std::lock_guard<std::mutex> lk(capture_source_mtx_);
        if (capture_source_) {
            capture_source_->StopCapturing();
        }
        if (dda_capture_source_) {
            dda_capture_source_->StopCapturing();
        }
        if (gdi_capture_source_) {
            gdi_capture_source_->StopCapturing();
        }
    }
    if (app_shared_info_) {
        LOGI("RdApplication shutdown: shared info");
        app_shared_info_->Exit();
    }
    // Stop audio capture before teardown: otherwise the capture thread keeps
    // invoking data callbacks into components that are being destroyed.
    if (audio_capture_source_) {
        audio_capture_source_->StopProviding();
    }
    if (audio_capture_thread_ && audio_capture_thread_->IsJoinable()) {
        audio_capture_thread_->Join();
    }
    if (app_manager_) {
        LOGI("RdApplication shutdown: app manager");
        app_manager_->Exit();
    }
    if (encoder_thread_) {
        LOGI("RdApplication shutdown: encoder");
        encoder_thread_->Exit();
    }
    if (composition_root_) {
        LOGI("RdApplication shutdown: built-in modules");
        const auto completion = std::make_shared<std::promise<render::ModuleLifecycleResult>>();
        auto future = completion->get_future();
        static_cast<void>(
            composition_root_->RequestStop([completion](render::ModuleLifecycleResult result) { completion->set_value(std::move(result)); }));
        if (future.wait_until(shutdown_deadline) != std::future_status::ready) {
            LOGE("event=composition.stop component=rd_application code=ASYNC_SCOPE_DRAIN_TIMEOUT "
                 "outcome=timeout recoverable=false");
        } else if (const auto stopped = future.get(); !stopped) {
            LOGE("event=composition.stop component=rd_application code={} outcome=failed reason={}", render::StableErrorCode(stopped.error().code),
                 stopped.error().reason);
        }
    }
    if (module_registry_) {
        LOGI("RdApplication shutdown: module event routing");
        module_registry_->StopRouting();
        const auto async_runtime = context_ ? context_->GetAsyncRuntime() : std::shared_ptr<PxAsyncRuntime>{};
        if (async_runtime && !async_runtime->IsStopping() && !async_runtime->IsRuntimeThread()) {
            const auto shutdown_scope = PxAsyncScope::Create(async_runtime, PxAsyncLane::kControl);
            const auto completion = std::make_shared<std::promise<PxResult<void>>>();
            auto future = completion->get_future();
            const auto spawned = shutdown_scope && shutdown_scope->Spawn("application-webrtc-shutdown", [module_registry = module_registry_,
                                                                                                         shutdown_deadline, completion] {
                return StopApplicationWebRtcLibraries(module_registry, shutdown_deadline, completion);
            });
            if (!spawned || future.wait_until(shutdown_deadline) != std::future_status::ready) {
                LOGE("event=webrtc.callback_quiescence component=rd_application "
                     "code=WEBRTC_CALLBACK_QUIESCENCE_TIMEOUT operation=stop outcome=timeout recoverable=false");
                if (shutdown_scope) {
                    shutdown_scope->BeginStop();
                }
            } else if (const auto stopped = future.get(); !stopped) {
                LOGE("event=webrtc.callback_quiescence component=rd_application code={} "
                     "operation=stop outcome=failed recoverable={} reason={}",
                     stopped.Error().StableCode(), stopped.Error().retryable, stopped.Error().message);
            }
        } else {
            LOGE("event=webrtc.callback_quiescence component=rd_application code=ASYNC_RUNTIME_UNAVAILABLE "
                 "operation=stop outcome=deferred recoverable=false");
        }
        LOGI("RdApplication shutdown: concrete modules and WebRTC libraries");
        module_registry_->StopModules();
    }
    LOGI("RdApplication shutdown: owners released");
    LOGI("event=application.shutdown component=rd_application operation=ordered_shutdown outcome=finished duration_ms={}",
         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shutdown_started).count());

    if (main_thread_id_ != 0) {
        PostThreadMessage(main_thread_id_, WM_QUIT, 0, 0);
    }
    if (rdApp.get() == this) {
        rdApp.reset();
    }
}

// ------------------------------------------------------ //
// Windows
WinApplication::WinApplication(const AppParams& args) : RdApplication(args) {}

// The base destructor is invoked automatically after this destructor.
// Calling it explicitly here destroyed the RdApplication subobject twice
// and could terminate px_render with STATUS_HEAP_CORRUPTION on early exit.
WinApplication::~WinApplication() {
    Exit();
}

int WinApplication::Run() {
    // WebView never injects the graphics hook and must not depend on the
    // auxiliary DXGI address probe executable being deployed.
    if (!settings_->IsWebViewMode()) {
        LoadDxAddress();
    }
    return RdApplication::Run();
}

static std::function<BOOL(DWORD)> s_ctrl_handler;
static BOOL ConsoleHandler(DWORD signal) {
    if (s_ctrl_handler) {
        return s_ctrl_handler(signal);
    }
    return FALSE;
}

void WinApplication::Exit() {
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    s_ctrl_handler = {};
    RdApplication::Exit();
}

void WinApplication::CaptureControlC() {
    const auto weak_self = weak_from_this();
    s_ctrl_handler = [weak_self](DWORD signal) -> BOOL {
        if (signal == CTRL_C_EVENT) {
            const auto self = weak_self.lock();
            if (!self) {
                return FALSE;
            }
            std::cout << "CTRL+C detected, localVar value is "
                      << "\n";
            self->Exit();
            return TRUE;
        }
        return FALSE;
    };
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        LOGE("ERROR: Could not set control handler");
    }
}

void WinApplication::LoadDxAddress() {
    app_shared_message_ = DxAddressLoader::LoadDxAddress();
    if (app_shared_message_) {
        app_shared_message_->ipc_port_ = settings_->transmission_.listening_port_;
        app_shared_message_->self_size_ = sizeof(AppSharedMessage);
        app_shared_message_->enable_hook_events_ = 1;
    } else {
        LOGE("LoadDxAddress failed.");
    }
}

// Windows
// ------------------------------------------------------ //
} // namespace px
