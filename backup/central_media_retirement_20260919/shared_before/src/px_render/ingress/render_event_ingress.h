//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_EVENT_INGRESS_H
#define PX_RENDER_EVENT_INGRESS_H

#include <memory>
#include <string>

#include "px_render/architecture/events/render_event.h"
#include "px_render/network/webrtc/webrtc_transport_types.h"

namespace px {

class RdContext;
class RdApplication;
class RdStatistics;
class RenderModuleRegistry;
class NetworkEventIngress;
class MessageNotifier;

class RenderEventIngress : public std::enable_shared_from_this<RenderEventIngress> {
  public:
    explicit RenderEventIngress(const std::shared_ptr<RdApplication>& app);
    void ProcessWebRtcEvent(const std::string& source_id, const WebRtcEvent& event);
    void ProcessRenderEvent(const RenderEventEnvelope& event);

  private:
    void SendWebRtcAnswerSdpToRemote(const WebRtcAnswerSdpEvent& event);
    void SendWebRtcIceToRemote(const WebRtcIceEvent& event);
    void ProcessPanelStreamMessage(const std::shared_ptr<PanelStreamMessageEvent>& event);
    void ReportRelayAlive(const std::string& device_id, int64_t timestamp);

  private:
    std::shared_ptr<RdApplication> app_ = nullptr;
    std::shared_ptr<RdContext> context_ = nullptr;
    std::shared_ptr<RenderModuleRegistry> module_registry_ = nullptr;
    std::shared_ptr<NetworkEventIngress> network_ingress_ = nullptr;
    std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
    std::shared_ptr<RdStatistics> stat_ = nullptr;
};

} // namespace px

#endif // PX_RENDER_EVENT_INGRESS_H
