#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "px_common/data.h"
#include "px_relay_client/relay_client_sdk.h"
#include "px_relay_client/relay_net_client.h"
#include "relay_message.pb.h"

namespace px {
namespace {

class FakeRelayNetClient final : public RelayNetClient {
public:
    void Start() override { ++start_count_; }
    void Stop() override { ++stop_count_; }

    void PostBinaryMessage(const std::string& msg) override {
        sent_messages_.push_back(msg);
    }

    [[nodiscard]] bool IsAlive() override { return true; }

    void PostNetTask(std::function<void()>&& task) override {
        queued_tasks_.push_back(std::move(task));
    }

    void FireConnected() {
        if (srv_conn_cbk_) {
            srv_conn_cbk_();
        }
    }

    void RunQueuedTasks() {
        auto tasks = std::move(queued_tasks_);
        queued_tasks_.clear();
        for (auto& task : tasks) {
            task();
        }
    }

    void ClearSentMessages() { sent_messages_.clear(); }

    [[nodiscard]] int StartCount() const { return start_count_; }
    [[nodiscard]] int StopCount() const { return stop_count_; }
    [[nodiscard]] size_t SentMessageCount() const {
        return sent_messages_.size();
    }
    [[nodiscard]] size_t QueuedTaskCount() const {
        return queued_tasks_.size();
    }

private:
    int start_count_ = 0;
    int stop_count_ = 0;
    std::vector<std::string> sent_messages_;
    std::vector<std::function<void()>> queued_tasks_;
};

RelayClientSdkParam MakeSdkParam() {
    RelayClientSdkParam param;
    param.device_id_ = "local-device";
    param.remote_device_id_ = "remote-device";
    param.stream_id_ = "stream";
    param.device_name_ = "client";
    return param;
}

TEST(RelayClientSdkLifecycle, CallbackDoesNotRetainDestroyedSdk) {
    const auto net_client = std::make_shared<FakeRelayNetClient>();
    auto sdk = std::make_shared<RelayClientSdk>(MakeSdkParam(), net_client);
    int connected_count = 0;
    sdk->SetOnRelayServerConnectedCallback(
        [&connected_count]() { ++connected_count; });

    net_client->FireConnected();
    EXPECT_EQ(connected_count, 1);
    EXPECT_EQ(net_client->SentMessageCount(), 1U);

    sdk.reset();
    net_client->FireConnected();
    EXPECT_EQ(connected_count, 1);
    EXPECT_EQ(net_client->SentMessageCount(), 1U);
}

TEST(RelayClientSdkLifecycle, QueuedSendDropsAfterSdkDestruction) {
    const auto net_client = std::make_shared<FakeRelayNetClient>();
    auto sdk = std::make_shared<RelayClientSdk>(MakeSdkParam(), net_client);

    const auto created_room = std::make_shared<px_relay::RelayMessage>();
    created_room->mutable_create_room_resp()->set_device_id("local-device");
    created_room->mutable_create_room_resp()->set_remote_device_id(
        "remote-device");
    created_room->mutable_create_room_resp()->set_room_id("room");
    sdk->OnCreatedRoomResp(created_room);

    net_client->ClearSentMessages();
    sdk->RelayProtoMessage(Data::From("payload"));
    ASSERT_EQ(net_client->QueuedTaskCount(), 1U);

    sdk.reset();
    net_client->RunQueuedTasks();
    EXPECT_EQ(net_client->SentMessageCount(), 0U);
}

TEST(RelayClientSdkLifecycle, RepeatedStartStopUsesSameNetClient) {
    const auto net_client = std::make_shared<FakeRelayNetClient>();
    const auto sdk = std::make_shared<RelayClientSdk>(MakeSdkParam(), net_client);

    constexpr int kCycles = 20;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        sdk->Start();
        sdk->Stop();
    }

    EXPECT_EQ(net_client->StartCount(), kCycles);
    EXPECT_EQ(net_client->StopCount(), kCycles);
}

}  // namespace
}  // namespace px
