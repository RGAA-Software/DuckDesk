//
// Created by RGAA on 2024/1/25.
//

#ifndef TC_APPLICATION_NET_EVENT_ROUTER_H
#define TC_APPLICATION_NET_EVENT_ROUTER_H

#include <string>
#include <memory>
#include <string_view>
#include <set>
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px
{
    class WinEventReplayer;
    class RdApplication;
    class RdSettings;
    class Data;
    class RdStatistics;
    class MessageListener;
    class MessageNotifier;
    class Message;
    class RdContext;
    class PluginManager;

    class PluginNetEventRouter {
    public :
        explicit PluginNetEventRouter(const std::shared_ptr<RdApplication>& app);
        void ProcessNetEvent(const std::shared_ptr<PxPluginNetClientEvent>& event);
        void ProcessClientConnectedEvent(const std::shared_ptr<PxPluginClientConnectedEvent>& event);
        void ProcessClientDisConnectedEvent(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event);
        void ProcessCapturingMonitorInfoEvent(const std::shared_ptr<PxPluginCapturingMonitorInfoEvent>& event) const;
        void ProcessEncodedAudioFrameEvent(const std::shared_ptr<Data>& data, int samples, int channels, int bits, int frame_size);
        void ProcessRtcReportEvent(const std::shared_ptr<PxPluginRtcReportEvent>& event);

    private:
        void ProcessHelloEvent(std::shared_ptr<Message>&& msg);
        void ProcessMouseEvent(std::shared_ptr<Message>&& msg);
        void ProcessKeyboardEvent(std::shared_ptr<Message>&& msg);
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

        // client -> render 窗口失焦
        //void ProcessFocusOutEvent();

        // client -> render 退出
        //void ProcessExitControlledEnd();

        void SyncInfoToUdpPlugin(int64_t socket_fd, const std::string& device_id, const std::string& stream_id);

        // report client connect/disconnect state
        void ReportClientConnected(const std::shared_ptr<PxPluginClientConnectedEvent>& event);
        void ReportClientDisConnected(const std::shared_ptr<PxPluginClientDisConnectedEvent>& event);

        // ack
        void ProcessAck(const std::shared_ptr<PxPluginNetClientEvent>& ev, const std::shared_ptr<Message>& m);

    private:
        RdSettings* settings_ = nullptr;
        RdStatistics* statistics_ = nullptr;
        std::shared_ptr<RdApplication> app_ = nullptr;
        std::shared_ptr<RdContext> context_ = nullptr;
        //std::shared_ptr<WinEventReplayer> win_event_replayer_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;

        // hook 模式：跟踪按下的键/鼠标键，客户端断开时补发释放事件
        std::set<uint32_t> pressed_keys_;
        std::set<int32_t> pressed_mouse_buttons_;
        int last_mouse_x_ = 0;
        int last_mouse_y_ = 0;
    };
}

#endif //TC_APPLICATION_MESSAGE_PROCESSOR_H
