//
// Created by RGAA on 1/03/2025.
//

#ifndef GAMMARAY_RELAY_CALLBACKS_H
#define GAMMARAY_RELAY_CALLBACKS_H

#include <string>
#include <memory>
#include <functional>

namespace relay
{
    class RelayMessage;
}

namespace tc
{

    using OnRelayServerConnected = std::function<void()>;
    using OnRelayServerDisConnected = std::function<void()>;
    using OnRelayServerHello = std::function<void(const std::string& device_id)>;
    using OnRelayServerHeartbeat = std::function<void(const std::string& device_id, int64_t hb_index)>;
    using OnRelayRoomPrepared = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;
    using OnRelayRoomDestroyed = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;
    using OnRelayRequestPausedStream = std::function<void()>;
    using OnRelayRequestResumeStream = std::function<void()>;
    using OnRelayError = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;
    using OnRelayRemoteDeviceOffline = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;
    using OnRelayNotification = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;
    using OnRelayRequestControl = std::function<void(std::shared_ptr<relay::RelayMessage> msg)>;

}

#endif //GAMMARAY_RELAY_CALLBACKS_H
