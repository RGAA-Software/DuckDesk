//
// Created by RGAA on 2024/1/25.
//

#include "plugin_net_event_router.h"
#include <memory>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <px_common_new/win32/win_helper.h>

#include "rd_app.h"
#include "rd_context.h"
#include "rd_statistics.h"
#include "px_message.pb.h"
#include "plugin_manager.h"
#include "app/app_manager.h"
#include "app/app_messages.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "app_global_messages.h"
#include "settings/rd_settings.h"
#include "network/net_message_maker.h"
#include "px_common_new/process_util.h"
#include "px_render_panel_message.pb.h"
#include "app/win/win_desktop_manager.h"
#include "px_encoder_new/encoder_messages.h"
#include "px_message_new/rp_proto_converter.h"
#include "px_message_new/proto_converter.h"
#include "px_render/plugins/net_ws/ws_user_proxy_router.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_common_new/win32/process_helper.h"
#include "px_capture_new/capture_message_maker.h"
#include "px_render/plugin_interface/px_video_encoder_plugin.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "px_render/plugin_interface/px_stream_plugin.h"

namespace px {

    struct PendingVirtualDisplayRequest {
        std::string device_id;
        std::string stream_id;
        std::chrono::steady_clock::time_point deadline;
        uint64_t required_capture_epoch = 0;
        std::optional<MsgVirtualDisplayServiceResult> service_result;
    };

    struct CachedVirtualDisplayResponse {
        std::string device_id;
        std::string stream_id;
        VirtualDisplayResponse response;
    };

    struct VirtualDisplayCoordinator {
        std::mutex mutex;
        uint64_t capture_epoch = 0;
        uint64_t first_frame_epoch = 0;
        std::unordered_map<std::string, PendingVirtualDisplayRequest> pending;
        std::unordered_map<std::string, CachedVirtualDisplayResponse> completed;
    };

    namespace {
        void SendVirtualDisplayResponse(
            const std::shared_ptr<RdApplication>& app,
            const std::string& device_id,
            const std::string& stream_id,
            const VirtualDisplayResponse& response) {
            px::Message message;
            message.set_type(kVirtualDisplayResponse);
            message.set_device_id(device_id);
            message.set_stream_id(stream_id);
            *message.mutable_virtual_display_response() = response;
            app->PostNetMessage(ProtoAsData(&message));
        }

        VirtualDisplayResponse BuildVirtualDisplayResponse(
            const MsgVirtualDisplayServiceResult& result,
            VirtualDisplayResponseState state) {
            VirtualDisplayResponse response;
            response.set_request_id(result.request_id_);
            response.set_accepted(result.accepted_);
            response.set_state(state);
            response.set_topology_changed(result.topology_changed_);
            response.set_topology_generation(result.topology_generation_);
            response.set_logical_display_id(result.logical_display_id_);
            response.set_error_code(result.error_code_);
            response.set_error_message(result.error_message_);
            response.set_owned_display_count(result.owned_display_count_);
            response.set_actual_usbmmidd_count(result.actual_usbmmidd_count_);
            response.set_driver_installed(result.driver_installed_);
            response.set_package_valid(result.package_valid_);
            response.set_removal_safe(result.removal_safe_);
            return response;
        }

        VirtualDisplayResponse BuildVirtualDisplayFailure(
            const std::string& request_id,
            const std::string& code,
            const std::string& message) {
            VirtualDisplayResponse response;
            response.set_request_id(request_id);
            response.set_accepted(false);
            response.set_state(kVirtualDisplayFailed);
            response.set_error_code(code);
            response.set_error_message(message);
            return response;
        }

        void CacheResponseLocked(
            VirtualDisplayCoordinator& coordinator,
            const std::string& request_id,
            CachedVirtualDisplayResponse&& cached) {
            if (coordinator.completed.size() >= 256) {
                coordinator.completed.clear();
            }
            coordinator.completed[request_id] = std::move(cached);
        }

        void CompleteVirtualDisplayRequests(
            const std::shared_ptr<RdApplication>& app,
            const std::shared_ptr<VirtualDisplayCoordinator>& coordinator) {
            std::vector<CachedVirtualDisplayResponse> ready;
            {
                std::scoped_lock lock(coordinator->mutex);
                for (auto it = coordinator->pending.begin(); it != coordinator->pending.end();) {
                    if (!it->second.service_result) {
                        ++it;
                        continue;
                    }
                    const auto& result = *it->second.service_result;
                    const bool capture_ready =
                        coordinator->capture_epoch >= it->second.required_capture_epoch &&
                        coordinator->first_frame_epoch >= it->second.required_capture_epoch;
                    if (result.accepted_ && result.topology_changed_ && !capture_ready) {
                        ++it;
                        continue;
                    }
                    const auto state = !result.accepted_
                        ? kVirtualDisplayFailed
                        : (result.topology_changed_ ? kVirtualDisplayNeedReconnect : kVirtualDisplayReady);
                    CachedVirtualDisplayResponse completed {
                        .device_id = it->second.device_id,
                        .stream_id = it->second.stream_id,
                        .response = BuildVirtualDisplayResponse(result, state),
                    };
                    CacheResponseLocked(*coordinator, it->first, CachedVirtualDisplayResponse(completed));
                    ready.push_back(std::move(completed));
                    it = coordinator->pending.erase(it);
                }
            }
            for (const auto& item : ready) {
                SendVirtualDisplayResponse(app, item.device_id, item.stream_id, item.response);
            }
        }

        void ExpireVirtualDisplayRequests(
            const std::shared_ptr<RdApplication>& app,
            const std::shared_ptr<VirtualDisplayCoordinator>& coordinator) {
            std::vector<CachedVirtualDisplayResponse> expired;
            const auto now = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(coordinator->mutex);
                for (auto it = coordinator->pending.begin(); it != coordinator->pending.end();) {
                    if (it->second.deadline > now) {
                        ++it;
                        continue;
                    }
                    const auto code = it->second.service_result
                        ? "CAPTURE_REBUILD_TIMEOUT" : "SERVICE_TIMEOUT";
                    const auto message = it->second.service_result
                        ? "display topology changed but capture did not produce a frame in time"
                        : "px_service did not answer the virtual display request in time";
                    CachedVirtualDisplayResponse completed {
                        .device_id = it->second.device_id,
                        .stream_id = it->second.stream_id,
                        .response = BuildVirtualDisplayFailure(it->first, code, message),
                    };
                    CacheResponseLocked(*coordinator, it->first, CachedVirtualDisplayResponse(completed));
                    expired.push_back(std::move(completed));
                    it = coordinator->pending.erase(it);
                }
            }
            for (const auto& item : expired) {
                SendVirtualDisplayResponse(app, item.device_id, item.stream_id, item.response);
            }
        }
    }

    PluginNetEventRouter::PluginNetEventRouter(const std::shared_ptr<RdApplication>& app) {
        this->app_ = app;
        this->context_ = app->GetContext();
        this->plugin_manager_ = app->GetPluginManager();
        this->settings_ = RdSettings::Instance();
        this->statistics_ = RdStatistics::Instance();
        virtual_display_ = std::make_shared<VirtualDisplayCoordinator>();

        msg_notifier_ = this->app_->GetContext()->GetMessageNotifier();
        msg_listener_ = this->app_->GetContext()->GetMessageNotifier()->CreateListener();
        msg_listener_->Listen<CaptureMonitorInfoMessage>([=, this](const CaptureMonitorInfoMessage& msg) {
            if (auto plugin = plugin_manager_->GetEventsReplayerPlugin(); plugin) {
                plugin->UpdateCaptureMonitorInfo(msg);
            }
            {
                std::scoped_lock lock(virtual_display_->mutex);
                ++virtual_display_->capture_epoch;
            }
            CompleteVirtualDisplayRequests(app_, virtual_display_);
        });
        msg_listener_->Listen<MsgCaptureTopologyFirstFrame>([=, this](const MsgCaptureTopologyFirstFrame&) {
            {
                std::scoped_lock lock(virtual_display_->mutex);
                virtual_display_->first_frame_epoch = virtual_display_->capture_epoch;
            }
            CompleteVirtualDisplayRequests(app_, virtual_display_);
        });
        msg_listener_->Listen<MsgTimer1000>([=, this](const MsgTimer1000&) {
            ExpireVirtualDisplayRequests(app_, virtual_display_);
        });
    }

    void PluginNetEventRouter::ProcessClientConnectedEvent(const std::shared_ptr<PxPluginClientConnectedEvent>& event) {
        // has no effects in plugin mode
        context_->SendAppMessage(MsgInsertIDR {});
        context_->SendAppMessage(MsgRefreshScreen{});
        LOGI("New connection established!");

        // tell all plugins that a client connected
        plugin_manager_->VisitAllPlugins([=](PxPluginInterface* plugin) {
            plugin->OnNewClientConnected(event->visitor_device_id_, event->stream_id_, event->conn_type_);
        });

        // tell encoder plugins to insert an I Frame
        plugin_manager_->VisitEncoderPlugins([=, this](PxVideoEncoderPlugin* plugin) {
            plugin->InsertIdr();
        });

        // active the password inputting ui
        if (WinHelper::IsSessionLocked()) {
            LOGI("SessionLocked, send a ctrl+alt+delete");
            app_->ReqCtrlAltDelete(event->visitor_device_id_, event->stream_id_);
        }

        // notify
        context_->SendAppMessage(MsgClientConnected {
            .conn_id_ = event->conn_id_,
            .conn_type_ = event->conn_type_,
            .stream_id_ = event->stream_id_,
            .visitor_device_id_ = event->visitor_device_id_,
            .begin_timestamp_ = event->begin_timestamp_,
        });

        // hook 模式：新客户端连接时让 DLL 丢弃积压事件并重置差分基准/修饰键，
        // 否则上一会话的残留会让游戏先看到一个巨大的位移跳变，且积压导致开始几秒无响应
        if (settings_->IsGameHookMode() && settings_->can_be_operated_) {
            auto reset_msg = CaptureResetInputMessage{};
            PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(reset_msg));
            pressed_keys_.clear();
            pressed_mouse_buttons_.clear();
        }

        // report it
        ReportClientConnected(event);

        // WebRTC 接入时若主管线已是 H265/全彩:浏览器解不了,主动提示
        if (event->conn_type_ == "RTC") {
            const bool full_color = settings_->EnableFullColorMode();
            const bool hevc = full_color
                || statistics_->video_encoder_format_ == Encoder::EncoderFormat::kHEVC;
            if (hevc) {
                const std::string reason = full_color ? "full_color" : "encoder_format";
                LOGW("WebRTC connected while pipeline is H265 ({}), notify client", reason);
                auto tip = NetMessageMaker::MakeVideoCodecChanged(px::VideoType::kNetHevc, full_color, reason);
                plugin_manager_->VisitNetPlugins([=](PxNetPlugin* plugin) {
                    if (plugin && plugin->GetPluginId() == kNetRtcLocalPluginId) {
                        plugin->PostProtoMessage(tip, false);
                    }
                });
            }
        }
    }

    void PluginNetEventRouter::ProcessClientDisConnectedEvent(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event) {
        MsgClientDisconnected msg{};
        msg.conn_id_ = event->conn_id_;
        msg.visitor_device_id_ = event->visitor_device_id_;
        msg.stream_id_ = event->stream_id_;
        msg.end_timestamp_ = event->end_timestamp_;
        msg.duration_ = event->duration_;
        context_->SendAppMessage(msg);

        // tell all plugins that a client disconnected
        plugin_manager_->VisitAllPlugins([=](PxPluginInterface* plugin) {
            plugin->OnClientDisconnected(event->visitor_device_id_, event->stream_id_);
        });

        // hook 模式：客户端断开时补发所有按下的键/鼠标键的释放事件，
        // 否则游戏内按键会一直处于按住状态
        if (settings_->IsGameHookMode() && settings_->can_be_operated_
            && (!pressed_keys_.empty() || !pressed_mouse_buttons_.empty())) {
            if (auto hwnd = this->app_->GetAppManager()->GetWindowHandle();
                hwnd && IsWindow(static_cast<HWND>(hwnd))) {
                auto hwnd_ptr = reinterpret_cast<uint64_t>(hwnd);
                for (auto key : pressed_keys_) {
                    auto msg = CaptureMessageMaker::MakeKeyboardEventMessage(hwnd_ptr, key, 0, 0, 0);
                    PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(msg));
                }
                for (auto btn : pressed_mouse_buttons_) {
                    auto msg = CaptureMessageMaker::MakeMouseEventMessage(
                        hwnd_ptr, last_mouse_x_, last_mouse_y_, btn, 0, false, true);
                    PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(msg));
                }
                LOGI("client disconnected: released {} keys, {} mouse buttons",
                     pressed_keys_.size(), pressed_mouse_buttons_.size());
            }
            pressed_keys_.clear();
            pressed_mouse_buttons_.clear();
        }

        // report it
        ReportClientDisConnected(event);
    }

    void PluginNetEventRouter::ProcessCapturingMonitorInfoEvent(const std::shared_ptr<PxPluginCapturingMonitorInfoEvent>& event) const {
        LOGI("Will Update CaptureMonitorInfo to replayer plugin.");
        app_->UpdateCapturingMonitorInfo();

        // Send monitor changed message
        if (const auto plugin = app_->GetWorkingMonitorCapturePlugin()) {
            const auto cm_msg = CaptureMonitorInfoMessage {
                .monitors_ = plugin->GetCaptureMonitorInfo(),
                .capturing_monitor_name_ = plugin->GetCapturingMonitorName(),
                .virtual_desktop_bound_rectangle_info_ = plugin->GetVirtualDesktopBoundRectangleInfo()
            };
            msg_notifier_->SendAppMessage(cm_msg);
        }
    }

    void PluginNetEventRouter::ProcessNetEvent(const std::shared_ptr<PxPluginNetClientEvent>& event) {
        if (event->is_proto_ && event->message_) {
            auto msg = std::make_shared<Message>();
            auto parse_res = msg->ParsePartialFromArray(event->message_->CStr(), event->message_->Size());
            if (!parse_res) {
                std::cout << "PluginNetEventRouter HandleMessage parse error" << std::endl;
                return;
            }

            // notify to all plugins (skip EventReplayer SendInput when hook-inner)
            const bool hook_inner = settings_->GetInputTarget() != InputTarget::kSystemSendInput;
            const bool is_input = msg->type() == MessageType::kMouseEvent ||
                                  msg->type() == MessageType::kKeyEvent ||
                                  msg->type() == MessageType::kTextInput;
            plugin_manager_->VisitAllPlugins([=, this](PxPluginInterface* plugin) {
                if (hook_inner && is_input &&
                    plugin->GetPluginId() == kEventReplayerPluginId) {
                    return;
                }
                plugin->OnMessage(msg);
            });

#if PX_USER_PROXY_ENABLED
            if (msg->type() == MessageType::kClipboardInfo) {
                LOGI("[LAT-clip] render recv kClipboardInfo, type: {}, files: {}, len: {}", (int)msg->clipboard_info().type(), msg->clipboard_info().files_size(), event->message_->Size());
                context_->PostTask([=, this]() {
                    bool user_proxy_connected = false;
                    plugin_manager_->VisitNetPlugins([&](PxNetPlugin* plugin) {
                        if (plugin->IsUserProxyConnected()) {
                            user_proxy_connected = true;
                        }
                    });
                    if (!user_proxy_connected) {
                        LOGW("user-proxy not connected, drop client clipboard, type={}, len={}",
                             (int)msg->clipboard_info().type(), event->message_->Size());
                        return;
                    }
                    pxrp::RpMessage rp_msg;
                    rp_msg.set_type(pxrp::kRpRawRenderMessage);
                    auto sub = rp_msg.mutable_raw_render_msg();
                    sub->set_msg(event->message_->AsString());
                    sub->set_data_channel(false);
                    sub->set_stream_id(msg->stream_id());
                    sub->set_device_id(msg->device_id());
                    LOGI("PostUserProxyMessage client clipboard, type={}, len={}",
                         (int)msg->clipboard_info().type(), event->message_->Size());
                    app_->PostUserProxyMessage(RpProtoAsData(&rp_msg));
                });
            } else if (msg->type() == MessageType::kClipboardReqBuffer ||
                       msg->type() == MessageType::kClipboardRespBuffer) {
                LOGI("[LAT-clip] render recv clipboard buffer msg, type: {}, len: {}",
                     (int)msg->type(), event->message_->Size());
                context_->PostTask([=, this]() {
                    bool user_proxy_connected = false;
                    plugin_manager_->VisitNetPlugins([&](PxNetPlugin* plugin) {
                        if (plugin->IsUserProxyConnected()) {
                            user_proxy_connected = true;
                        }
                    });
                    if (!user_proxy_connected) {
                        LOGW("user-proxy not connected, drop clipboard buffer msg, type={}, len={}",
                             (int)msg->type(), event->message_->Size());
                        return;
                    }
                    pxrp::RpMessage rp_msg;
                    rp_msg.set_type(pxrp::kRpRawRenderMessage);
                    auto sub = rp_msg.mutable_raw_render_msg();
                    sub->set_msg(event->message_->AsString());
                    sub->set_data_channel(true);
                    sub->set_stream_id(msg->stream_id());
                    sub->set_device_id(msg->device_id());
                    LOGI("PostUserProxyMessage clipboard buffer msg, type={}, len={}",
                         (int)msg->type(), event->message_->Size());
                    app_->PostUserProxyMessage(RpProtoAsData(&rp_msg));
                });
            }
#else
            // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
            // notify to panel
            context_->PostTask([=, this]() {
                pxrp::RpMessage msg;
                msg.set_type(pxrp::kRpRawRenderMessage);
                auto sub = msg.mutable_raw_render_msg();
                sub->set_msg(event->message_->AsString());
                auto buffer = RpProtoAsData(&msg);
                app_->PostPanelMessage(buffer);
            });
#endif

            switch (msg->type()) {
                case kHello: {
                    this->ProcessHelloEvent(std::move(msg));
                    if (event->nt_plugin_type_ == NetPluginType::kUdpKcp) {
                        this->SyncInfoToUdpPlugin(event->socket_fd_, msg->device_id(), msg->stream_id());
                    }
                    break;
                }
                case kAck: {
                    this->ProcessAck(event, msg);
                    break;
                }
                case kHeartBeat: {
                    ProcessHeartBeat(std::move(msg));
                    if (event->nt_plugin_type_ == NetPluginType::kUdpKcp) {
                        this->SyncInfoToUdpPlugin(event->socket_fd_, msg->device_id(), msg->stream_id());
                    }
                    break;
                }
                case MessageType::kClientStatistics: {
                    ProcessClientStatistics(std::move(msg));
                    break;
                }
                case kClipboardInfo: {
                    const auto& sub = msg->clipboard_info();
                    LOGI("Clipboard msg: {}", sub.msg());
                    LOGI("Clipboard files: {}", sub.files_size());
                    for (const auto& file : sub.files()) {
                        LOGI("File: {}", file.full_path());
                    }
                    break;
                }
                case kClipboardInfoResp: {
                    break;
                }
                case kSwitchMonitor: {
                    ProcessSwitchMonitor(std::move(msg));
                    break;
                }
                case kSwitchWorkMode: {
                    ProcessSwitchWorkMode(std::move(msg));
                    break;
                }
                case kChangeMonitorResolution: {
                    ProcessChangeMonitorResolution(std::move(msg));
                    break;
                }
                case kInsertKeyFrame: {
                    ProcessInsertKeyFrame(std::move(msg));
                    break;
                }
                case kReqCtrlAltDelete: {
                    LOGW("Request the CtrlAltDelete");
                    ProcessCtrlAltDelete(std::move(msg));
                    break;
                }
                case MessageType::kClipboardReqAtBegin:
                case MessageType::kClipboardReqAtEnd:
                case MessageType::kClipboardReqBuffer:
                case MessageType::kClipboardRespBuffer: {
                    //if (auto plugin = plugin_manager_->GetClipboardPlugin(); plugin) {
                    //    plugin->OnMessage(msg);
                    //}
                    break;
                }
                case MessageType::kUpdateDesktop: {
                    ProcessUpdateDesktop();
                    break;
                }
                case MessageType::kHardUpdateDesktop: {
                    ProcessHardUpdateDesktop();
                    break;
                }
                case kSwitchFullColorMode: {
                    ProcessSwitchFullColorMode(std::move(msg));
                    break;
                }
                case kStartMediaRecordClientSide: {
                    ProcessStartMediaRecordClientSide();
                    break;
                }
                case kStopMediaRecordClientSide: {
                    ProcessStopMediaRecordClientSide();
                    break;
                }
                case kModifyFps: {
                    ProcessModifyFps(std::move(msg));
                    break;
                }
                case kVirtualDisplayRequest: {
                    ProcessVirtualDisplayRequest(std::move(msg));
                    break;
                }
                case MessageType::kMouseEvent: {
                    if (!settings_->app_.IsGlobalReplayMode()) {
                        ProcessMouseEvent(std::move(msg));
                    }
                    break;
                }
                case MessageType::kKeyEvent: {
                    if (!settings_->app_.IsGlobalReplayMode()) {
                        ProcessKeyboardEvent(std::move(msg));
                    }
                    break;
                }
                case MessageType::kTextInput: {
                    ProcessTextInput(std::move(msg));
                    break;
                }
//                case kFocusOutEvent: {
//                    ProcessFocusOutEvent();
//                    break;
//                }
//                case kExitControlledEnd: {
//                    ProcessExitControlledEnd();
//                    break;
//                }
                case kStopRender: {
                    // A game-hook render owns the game in a kill-on-close job.
                    // A browser opening a stream must never be able to end that
                    // game through a control-packet mismatch. Console owns the
                    // lifecycle of game-hook instances and sends kSrvStopServer
                    // over its authenticated service channel instead.
                    LOGW("kStopRender received from client: device={}, stream={}",
                         msg->device_id(), msg->stream_id());
                    if (settings_->IsGameHookMode() || settings_->IsWebViewMode()) {
                        LOGW("Ignore client kStopRender for Console application instance; use Console stop action instead.");
                        break;
                    }
                    ProcessUtil::KillProcess(GetCurrentProcessId());
                    break;
                }
                case kLockDevice: {
                    context_->SendAppMessage(MsgPanelStreamLockScreen{});
                }
                default: {
                   
                }
            }
        } else {

        }
    }

    void PluginNetEventRouter::ProcessHelloEvent(std::shared_ptr<Message>&& msg) {
        const auto& hello = msg->hello();
        auto event = MsgClientHello();
        event.device_id_ = msg->device_id();
        event.stream_id_ = msg->stream_id();
        event.enable_audio_ = hello.enable_audio();
        event.enable_video_ = hello.enable_video();
        event.enable_controller = hello.enable_controller();
        event.client_type_ = hello.client_type();
        event.device_name_ = hello.device_name();
        app_->GetContext()->SendAppMessage(event);

        auto e = std::make_shared<MsgClientHello>(event);
        plugin_manager_->VisitAllPlugins([=, this](PxPluginInterface* plugin) {
            plugin->DispatchAppEvent(e);
        });
    }

    void PluginNetEventRouter::ProcessMouseEvent(std::shared_ptr<Message>&& msg) {
        if (settings_->app_.IsGlobalReplayMode()) {
            // Desktop: EventReplayerPlugin handles via OnMessage → SendInput.
            return;
        }
        if (!settings_->can_be_operated_) {
            return;
        }
        const auto& mouse_event = msg->mouse_event();
        if (settings_->IsWebViewMode()) {
            app_->SendWebViewMouseEvent(mouse_event);
            return;
        }
        auto hwnd = this->app_->GetAppManager()->GetWindowHandle();
        auto hwnd_ptr = reinterpret_cast<uint64_t>(hwnd);
        if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("hook-inner mouse: no game HWND yet, drop n={}", s_n);
            }
            return;
        }
        RECT rect{0, 0, 0, 0};
        if (!ProcessHelper::GetWindowPositionByHwnd(static_cast<HWND>(hwnd), rect)) {
            LOGE("GetWindowPositionByHwnd failed for HWND: {:x}", hwnd_ptr);
            return;
        }

        int app_width = rect.right - rect.left;
        int app_height = rect.bottom - rect.top;
        if (app_width <= 0 || app_height <= 0) {
            LOGW("hook-inner mouse: invalid window size {}x{}", app_width, app_height);
            return;
        }

        auto x = rect.left + app_width * mouse_event.x_ratio();
        auto y = rect.top + app_height * mouse_event.y_ratio();

        // 记录当前按下的鼠标键 / 最近一次坐标，用于客户端断开时补发释放事件
        if (mouse_event.pressed()) {
            pressed_mouse_buttons_.insert(mouse_event.button());
        } else if (mouse_event.released()) {
            pressed_mouse_buttons_.erase(mouse_event.button());
        }
        last_mouse_x_ = (int)x;
        last_mouse_y_ = (int)y;

        auto mouse_event_msg = CaptureMessageMaker::MakeMouseEventMessage(
            hwnd_ptr, (int)x, (int)y, mouse_event.button(), mouse_event.data(),
            mouse_event.pressed(), mouse_event.released());
        {
            static thread_local uint64_t s_n = 0;
            const auto n = ++s_n;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("hook-inner mouse: n={} hwnd={:x} screen=({},{}) ratio=({:.3f},{:.3f}) btn={}",
                     n, hwnd_ptr, (int)x, (int)y, mouse_event.x_ratio(), mouse_event.y_ratio(),
                     mouse_event.button());
            }
        }
        PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(mouse_event_msg));
    }

    void PluginNetEventRouter::ProcessKeyboardEvent(std::shared_ptr<Message>&& msg) {
        if (settings_->app_.IsGlobalReplayMode()) {
            return;
        }
        if (!settings_->can_be_operated_) {
            return;
        }
        const auto& key_event = msg->key_event();
        if (settings_->IsWebViewMode()) {
            app_->SendWebViewKeyEvent(key_event);
            return;
        }
        auto hwnd = this->app_->GetAppManager()->GetWindowHandle();
        auto hwnd_ptr = reinterpret_cast<uint64_t>(hwnd);
        if (!hwnd || !IsWindow(static_cast<HWND>(hwnd))) {
            static thread_local uint64_t s_n = 0;
            if (++s_n == 1 || (s_n % 100) == 0) {
                LOGW("hook-inner key: no game HWND yet, drop n={}", s_n);
            }
            return;
        }

        // 记录当前按下的键，用于客户端断开时补发释放事件
        if (key_event.down()) {
            pressed_keys_.insert(key_event.key_code());
        } else {
            pressed_keys_.erase(key_event.key_code());
        }

        auto keyboard_msg = CaptureMessageMaker::MakeKeyboardEventMessage(
            hwnd_ptr, key_event.key_code(), key_event.down(), key_event.num_lock_status(),
            key_event.caps_lock_status());
        {
            static thread_local uint64_t s_n = 0;
            const auto n = ++s_n;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("hook-inner key: n={} hwnd={:x} key=0x{:x} down={}",
                     n, hwnd_ptr, key_event.key_code(), key_event.down());
            }
        }
        PostIpcMessage(CaptureMessageMaker::ConvertMessageToString(keyboard_msg));
    }

    void PluginNetEventRouter::ProcessTextInput(std::shared_ptr<Message>&& msg) {
        if (!settings_->can_be_operated_ ||
            settings_->GetInputTarget() != InputTarget::kCefBrowser) {
            return;
        }
        const auto& input = msg->text_input();
        if (input.text().empty() || input.text().size() > 4096) {
            return;
        }
        app_->SendWebViewTextInput(input);
    }

    void PluginNetEventRouter::PostIpcMessage(const std::string& msg) {
        auto task_msg = AppMessageMaker::MakeTaskMessage([=, this]() mutable {
            this->app_->PostIpcMessage(msg);
        });
        app_->PostGlobalAppMessage(std::move(task_msg));
    }

    void PluginNetEventRouter::ProcessClientStatistics(std::shared_ptr<Message>&& msg) {
        auto& cst = msg->client_statistics();
        statistics_->CopyDecodeDurations(cst.decode_durations());
        statistics_->CopyClientVideoRecvGaps(cst.video_recv_gaps());
        statistics_->client_fps_video_recv_ = cst.fps_video_recv();
        statistics_->client_fps_render_ = cst.fps_render();
        statistics_->client_recv_media_data_ = cst.recv_media_data();
        statistics_->render_width_ = cst.render_width();
        statistics_->render_height_ = cst.render_height();
    }

    void PluginNetEventRouter::ProcessHeartBeat(std::shared_ptr<Message>&& msg) {
        app_->PostGlobalTask([=, this]() {
            auto& hb = msg->heartbeat();
            auto proto_msg = NetMessageMaker::MakeOnHeartBeatMsg(app_, hb.index(), hb.timestamp());
            app_->PostNetMessage(proto_msg);
        });

        auto event = std::make_shared<MsgClientHeartbeat>();
        event->device_id_ = msg->device_id();
        event->stream_id_ = msg->stream_id();
        event->hb_index_ = msg->heartbeat().index();
        event->timestamp_ = msg->heartbeat().timestamp();
        plugin_manager_->VisitAllPlugins([=, this](PxPluginInterface* plugin) {
            plugin->DispatchAppEvent(event);
        });
    }

    void PluginNetEventRouter::ProcessClipboardInfo(std::shared_ptr<Message>&& msg) {
        //if (auto plugin = plugin_manager_->GetClipboardPlugin(); plugin) {
        //    plugin->OnMessage(msg);
        //}
    }

    void PluginNetEventRouter::ProcessSwitchMonitor(std::shared_ptr<Message>&& msg) {
        LOGI("ProcessSwitchMonitor, name: {}", msg->switch_monitor().name());
        app_->PostGlobalTask([=, this]() {
            auto sm = msg->switch_monitor();
            auto capture_plugin = app_->GetWorkingMonitorCapturePlugin();
            if (!capture_plugin) {
                return;
            }
            capture_plugin->SetCaptureMonitor(sm.name());
            //plugin->SendCapturingMonitorMessage();

            auto encoder_plugins = app_->GetWorkingVideoEncoderPlugins();
            for (const auto& [k, encoder_plugin] : encoder_plugins) {
                if (encoder_plugin) {
                    encoder_plugin->InsertIdr();
                }
            }

            // capturing monitor info
            app_->UpdateCapturingMonitorInfo();

            int mon_index = 0;
            auto mon_index_res = capture_plugin->GetMonIndexByName(sm.name());
            if (mon_index_res.has_value()) {
                mon_index = mon_index_res.value();
            }
            auto proto_msg = NetMessageMaker::MakeMonitorSwitched(sm.name(), mon_index);
            app_->PostNetMessage(proto_msg);
        });
    }

    void PluginNetEventRouter::ProcessSwitchWorkMode(std::shared_ptr<Message>&& msg) {
        app_->PostGlobalTask([=, this]() {
            auto wm = msg->work_mode();
            auto plugin = app_->GetWorkingMonitorCapturePlugin();
            if (!plugin) {
                LOGE("Working monitor capture is empty!");
                return;
            }
            if (wm.mode() == SwitchWorkMode::kWork) {
                plugin->SetCaptureFps(30);
            } else if (wm.mode() == SwitchWorkMode::kGame) {
                plugin->SetCaptureFps(60);
            }
        });
    }

    void PluginNetEventRouter::ProcessSwitchFullColorMode(std::shared_ptr<Message>&& msg) {
        auto sw = msg->switch_full_color_mode();
        this->settings_->SetFullColorMode(sw.enable());
    }

    void PluginNetEventRouter::ProcessStartMediaRecordClientSide() {
        auto encoder_plugins = app_->GetWorkingVideoEncoderPlugins();
        for (const auto& [k, encoder_plugin] : encoder_plugins) {
            if (encoder_plugin) {
                encoder_plugin->InsertIdr();
                encoder_plugin->SetClientSideMediaRecording(true);
            }
        }
    }

    void PluginNetEventRouter::ProcessStopMediaRecordClientSide() {
        auto encoder_plugins = app_->GetWorkingVideoEncoderPlugins();
        for (const auto& [k, encoder_plugin] : encoder_plugins) {
            if (encoder_plugin) {
                encoder_plugin->SetClientSideMediaRecording(false);
            }
        }
    }

    void PluginNetEventRouter::ProcessChangeMonitorResolution(std::shared_ptr<Message>&& msg) {
        app_->PostGlobalTask([=, this]() {
            auto cmr = msg->change_monitor_resolution();
            app_->ResetMonitorResolution(cmr.monitor_name(), cmr.target_width(), cmr.target_height());
        });
    }

    void PluginNetEventRouter::ProcessInsertKeyFrame(std::shared_ptr<Message>&& msg) {
        app_->PostGlobalTask([=, this]() {
            app_->SendAppMessage(MsgInsertKeyFrame{});
        });
    }

    void PluginNetEventRouter::ProcessEncodedAudioFrameEvent(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size) {
        auto net_msg = NetMessageMaker::MakeAudioFrameMsg(data, samples, channels, bits, frame_size);
        //statistics_->AppendMediaBytes(net_msg.size());
        app_->PostNetMessage(net_msg);

        // 编码音频分发给可选的 PxEncodedAudioSink 消费端(录制插件等, 零重编码)。
        // 用 dynamic_cast 检测实现者, 不改变基类虚表布局(与旧插件 DLL 保持 ABI 兼容)。
        // 与视频分发路径一致, 走 Stream 插件专用任务线程, 串行回调。
        auto pm = plugin_manager_;
        app_->GetContext()->PostStreamPluginTask([pm, data, samples, channels, bits, frame_size]() {
            pm->VisitStreamPlugins([=](PxStreamPlugin* plugin) {
                if (auto* sink = dynamic_cast<PxEncodedAudioSink*>(plugin)) {
                    sink->OnEncodedAudioFrame(data, samples, channels, bits, frame_size);
                }
            });
        });
    }

    void PluginNetEventRouter::ProcessCtrlAltDelete(std::shared_ptr<Message>&& msg) {
        app_->ReqCtrlAltDelete(msg->device_id(), msg->stream_id());
    }

    void PluginNetEventRouter::ProcessUpdateDesktop() {
        if (context_) {
            context_->SendAppMessage(MsgRefreshScreen{});
        }
    }

    void PluginNetEventRouter::ProcessHardUpdateDesktop() {
        auto desk_manager = app_->GetDesktopManager();
        if (!desk_manager) {
            return;
        }
        desk_manager->UpdateDesktop();
    }

    void PluginNetEventRouter::SyncInfoToUdpPlugin(int64_t socket_fd, const std::string& device_id, const std::string& stream_id) {
        auto udp_plugin = plugin_manager_->GetUdpPlugin();
        if (!udp_plugin) {
            return;
        }
        udp_plugin->SyncInfo(NetSyncInfo {
            .socket_fd_ = socket_fd,
            .device_id_ = device_id,
            .stream_id_ = stream_id
        });
    }

    void PluginNetEventRouter::ProcessRtcReportEvent(const std::shared_ptr<PxPluginRtcReportEvent>& event) {

    }

    void PluginNetEventRouter::ReportClientConnected(const std::shared_ptr<PxPluginClientConnectedEvent>& event) {
        app_->PostGlobalTask([=, this]() {
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpClientConnected);
            auto sub = msg.mutable_client_connected();
            sub->set_conn_id(event->conn_id_);
            sub->set_stream_id(event->stream_id_);
            sub->set_conn_type(event->conn_type_);
            sub->set_visitor_device_id(event->visitor_device_id_);
            sub->set_begin_timestamp(event->begin_timestamp_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
    }

    void PluginNetEventRouter::ReportClientDisConnected(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event) {
        app_->PostGlobalTask([=, this]() {
            pxrp::RpMessage msg;
            msg.set_type(pxrp::kRpClientDisConnected);
            auto sub = msg.mutable_client_disconnected();
            sub->set_conn_id(event->conn_id_);
            sub->set_stream_id(event->stream_id_);
            sub->set_visitor_device_id(event->visitor_device_id_);
            sub->set_end_timestamp(event->end_timestamp_);
            sub->set_duration(event->duration_);
            auto buffer = RpProtoAsData(&msg);
            app_->PostPanelMessage(buffer);
        });
    }

    // client -> render 修改帧率
    void PluginNetEventRouter::ProcessModifyFps(std::shared_ptr<Message>&& msg) {
        auto mf = msg->modify_fps();
        int fps = mf.fps();
        if (context_) {
            context_->SendAppMessage(MsgModifyFps{.fps_ = fps});
        }
    }

    void PluginNetEventRouter::ProcessVirtualDisplayRequest(std::shared_ptr<Message>&& msg) {
        const auto& request = msg->virtual_display_request();
        const auto request_id = request.request_id();
        const auto device_id = msg->device_id();
        const auto stream_id = msg->stream_id();

        const auto fail = [this, &device_id, &stream_id, &request_id](
                              const std::string& code, const std::string& message) {
            SendVirtualDisplayResponse(
                app_, device_id, stream_id,
                BuildVirtualDisplayFailure(request_id, code, message));
        };
        if (request_id.empty() || request.operation() < kRemoteVirtualDisplayCreate ||
            request.operation() > kRemoteVirtualDisplayResetOwned) {
            fail("INVALID_ARGUMENT", "invalid virtual display request");
            return;
        }
        if (!settings_->can_be_operated_) {
            fail("PERMISSION_DENIED", "the session is view-only");
            return;
        }
        if (!settings_->virtual_display_enabled_) {
            fail("FEATURE_DISABLED", "virtual display management is disabled on the controlled device");
            return;
        }
        if (settings_->IsGameHookMode()) {
            fail("UNSUPPORTED_CAPTURE_MODE", "virtual displays are only available in desktop capture mode");
            return;
        }

        std::optional<VirtualDisplayResponse> cached;
        bool already_pending = false;
        {
            std::scoped_lock lock(virtual_display_->mutex);
            if (const auto it = virtual_display_->completed.find(request_id);
                it != virtual_display_->completed.end()) {
                cached = it->second.response;
            }
            else if (virtual_display_->pending.contains(request_id)) {
                already_pending = true;
            }
            else {
                virtual_display_->pending.emplace(request_id, PendingVirtualDisplayRequest {
                    .device_id = device_id,
                    .stream_id = stream_id,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35),
                    .required_capture_epoch = virtual_display_->capture_epoch + 1,
                });
            }
        }
        if (cached) {
            SendVirtualDisplayResponse(app_, device_id, stream_id, *cached);
            return;
        }
        if (already_pending) {
            fail("REQUEST_IN_PROGRESS", "the same virtual display request is already running");
            return;
        }

        auto coordinator = virtual_display_;
        auto app = app_;
        auto context = context_;
        app_->RequestVirtualDisplay(
            request_id,
            static_cast<int>(request.operation()),
            request.width(),
            request.height(),
            request.refresh_hz(),
            [coordinator, app, context](const MsgVirtualDisplayServiceResult& result) {
                context->PostTask([coordinator, app, result]() {
                    {
                        std::scoped_lock lock(coordinator->mutex);
                        const auto it = coordinator->pending.find(result.request_id_);
                        if (it == coordinator->pending.end()) {
                            return;
                        }
                        it->second.service_result = result;
                    }
                    CompleteVirtualDisplayRequests(app, coordinator);
                });
            });
    }

//    void PluginNetEventRouter::ProcessFocusOutEvent() {
//        win_event_replayer_->HandleFocusOutEvent();
//    }

//    void PluginNetEventRouter::ProcessExitControlledEnd() {
//        LOGI("recv exit controlled end msg, render will exit and restart.");
//        //win_event_replayer_->SimulateCtrlWinShiftB();
//        //exit(0);
//    }

    void PluginNetEventRouter::ProcessAck(const std::shared_ptr<PxPluginNetClientEvent>& ev, const std::shared_ptr<Message>& m) {
        auto sub = m->ack();
        auto ack = std::make_shared<NetMessageAck>();
        ack->send_time_ = sub.send_time();
        ack->resp_time_ = sub.resp_time();
        ack->ch_type_ = ev->nt_channel_type_;
        ack->msg_type_ = m->type();
        ev->from_plugin_->OnMessageAck(ack);
    }
}
