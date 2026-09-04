//
// Created by RGAA on 2024/1/25.
//

#ifndef PX_NETWORK_EVENT_INGRESS_H
#define PX_NETWORK_EVENT_INGRESS_H

#include <string>
#include <memory>
#include <string_view>
#include <map>
#include <set>
#include <tuple>
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px
{
    namespace render {
        class InputReplayService;
        class JoystickService;
        class FileTransferService;
        class VoiceCallService;
    }
    class RdApplication;
    class RdSettings;
    class Data;
    class RdStatistics;
    class MessageListener;
    class MessageNotifier;
    class Message;
    class RdContext;
    class RenderModuleRegistry;
    struct VirtualDisplayCoordinator;

    class NetworkEventIngress : public std::enable_shared_from_this<NetworkEventIngress> {
    public :
        static std::shared_ptr<NetworkEventIngress> Make(
            const std::shared_ptr<RdApplication>& app);
        explicit NetworkEventIngress(const std::shared_ptr<RdApplication>& app);
        void ProcessNetEvent(const std::shared_ptr<PxPluginNetClientEvent>& event);
        void ProcessClientConnectedEvent(const std::shared_ptr<PxPluginClientConnectedEvent>& event);
        void ProcessClientDisConnectedEvent(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event);
        void ProcessCapturingMonitorInfoEvent(const std::shared_ptr<PxPluginCapturingMonitorInfoEvent>& event) const;
        void ProcessEncodedAudioFrameEvent(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size);
        void ProcessRtcReportEvent(const std::shared_ptr<PxPluginRtcReportEvent>& event);
        void ReleaseControllerInput(const LogicalSessionInputLease& lease);

    private:
        void InitListeners();
        void ProcessHelloEvent(std::shared_ptr<Message>&& msg);
        void ProcessMouseEvent(std::shared_ptr<Message>&& msg,
                               const LogicalSessionInputLease& lease);
        void ProcessKeyboardEvent(std::shared_ptr<Message>&& msg,
                                  const LogicalSessionInputLease& lease);
        void ProcessTextInput(std::shared_ptr<Message>&& msg);
        void PostIpcMessage(const std::string& msg);
        void ProcessClientStatistics(std::shared_ptr<Message>&& msg);
        void ProcessHeartBeat(std::shared_ptr<Message>&& msg);
        void ProcessClipboardInfo(std::shared_ptr<Message>&& msg);
        void ProcessSwitchMonitor(std::shared_ptr<Message>&& msg);
        void ProcessSwitchWorkMode(std::shared_ptr<Message>&& msg);
        void ProcessChangeMonitorResolution(std::shared_ptr<Message>&& msg);
        void ProcessInsertKeyFrame(std::shared_ptr<Message>&& msg);
        void ProcessCtrlAltDelete(std::shared_ptr<Message>&& msg);
        // 刷新桌面
        void ProcessUpdateDesktop();
        void ProcessHardUpdateDesktop();
        // 全彩模式
        void ProcessSwitchFullColorMode(std::shared_ptr<Message>&& msg);

        void ProcessStartMediaRecordClientSide();

        void ProcessStopMediaRecordClientSide();

        // client -> render 修改帧率
        void ProcessModifyFps(std::shared_ptr<Message>&& msg);
        void ProcessVirtualDisplayRequest(std::shared_ptr<Message>&& msg);

        // client -> render 窗口失焦
        //void ProcessFocusOutEvent();

        // client -> render 退出
        //void ProcessExitControlledEnd();

        void SyncInfoToUdpTransport(int64_t socket_fd, const std::string& device_id, const std::string& stream_id);

        // report client connect/disconnect state
        void ReportClientConnected(const std::shared_ptr<PxPluginClientConnectedEvent>& event);
        void ReportClientDisConnected(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event);

        // ack
        void ProcessAck(const std::shared_ptr<PxPluginNetClientEvent>& ev, const std::shared_ptr<Message>& m);
        void SendRtcSignalingError(const std::string& stream_id,
                                   const std::string& code,
                                   const std::string& message) const;

        struct InputLeaseKey {
            std::string logical_session_id_;
            uint64_t generation_ = 0;

            [[nodiscard]] bool operator<(const InputLeaseKey& other) const {
                return std::tie(logical_session_id_, generation_)
                    < std::tie(other.logical_session_id_, other.generation_);
            }
        };

        struct InputState {
            std::set<uint32_t> pressed_keys_;
            std::set<int32_t> pressed_mouse_buttons_;
            int last_mouse_x_ = 0;
            int last_mouse_y_ = 0;
        };

        static InputLeaseKey ToInputLeaseKey(const LogicalSessionInputLease& lease);

    private:
        // Process-lifetime settings singleton; non-null by construction.
        RdSettings& settings_;
        std::shared_ptr<RdStatistics> statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        std::shared_ptr<render::InputReplayService> input_replay_service_;
        std::shared_ptr<render::JoystickService> joystick_service_;
        std::shared_ptr<render::FileTransferService> file_transfer_service_;
        std::shared_ptr<render::VoiceCallService> voice_call_service_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<VirtualDisplayCoordinator> virtual_display_ = nullptr;

        // Hook-mode input is tracked by the owner and generation of the
        // controller lease. A replacement lease can never release a new
        // controller's keys, nor retain the old controller's keys.
        std::map<InputLeaseKey, InputState> input_states_;
    };
}

#endif //PX_NETWORK_EVENT_INGRESS_H
