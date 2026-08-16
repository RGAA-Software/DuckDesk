//
// Created by RGAA on 1/03/2025.
//

#ifndef PX_RELAY_CALLBACKS_H
#define PX_RELAY_CALLBACKS_H

#include <string>
#include <memory>
#include <functional>

namespace px_relay
{
    class RelayMessage;
}

namespace px
{

    using OnRelayServerConnected = std::function<void()>;
    using OnRelayServerDisConnected = std::function<void()>;
    using OnRelayServerHello = std::function<void(const std::string& device_id)>;
    using OnRelayServerHeartbeat = std::function<void(const std::string& device_id, int64_t hb_index)>;
    using OnRelayRoomPrepared = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;
    using OnRelayRoomDestroyed = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;
    using OnRelayRequestPausedStream = std::function<void()>;
    using OnRelayRequestResumeStream = std::function<void()>;
    using OnRelayError = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;
    using OnRelayRemoteDeviceOffline = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;
    using OnRelayNotification = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;
    using OnRelayRequestControl = std::function<void(std::shared_ptr<px_relay::RelayMessage> msg)>;

}

#endif //PX_RELAY_CALLBACKS_H
