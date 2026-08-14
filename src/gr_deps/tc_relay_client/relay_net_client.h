//
// Created by RGAA on 1/03/2025.
//

#ifndef GAMMARAY_RELAY_NET_CLIENT_H
#define GAMMARAY_RELAY_NET_CLIENT_H

#include "relay_callbacks.h"

namespace tc
{

    class Data;

    class RelayNetClient {
    public:
        virtual ~RelayNetClient() = default;

        virtual void Start() {

        }

        virtual void Stop() {

        }

        virtual void PostBinaryMessage(const std::string &msg) {

        }

        virtual void PostTextMessage(const std::string& msg) {

        }

        virtual void SyncDeviceId(const std::string& device_id) {

        }

        virtual void SetOnRelayServerConnectedCallback(OnRelayServerConnected&& cbk) {
            srv_conn_cbk_ = cbk;
        }

        virtual void SetOnRelayServerDisConnectedCallback(OnRelayServerDisConnected&& cbk) {
            srv_dis_conn_cbk_ = cbk;
        }

        virtual void SetOnRelayProtoMessageCallback(std::function<void(std::shared_ptr<Data>)>&& cbk) {
            msg_cbk_ = cbk;
        }

        virtual int64_t GetQueuingMsgCount() {
            return 0;
        }

        virtual bool IsAlive() = 0;

        virtual void PostNetTask(std::function<void()>&& task) = 0;

    protected:
        std::function<void(std::shared_ptr<Data>)> msg_cbk_;
        OnRelayServerConnected srv_conn_cbk_;
        OnRelayServerDisConnected srv_dis_conn_cbk_;
    };

}

#endif //GAMMARAY_RELAY_NET_CLIENT_H
