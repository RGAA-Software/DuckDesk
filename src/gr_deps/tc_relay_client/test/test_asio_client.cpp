//
// Created by RGAA on 18/09/2025.
//

#include <gtest/gtest.h>
#include "../relay_ws_client.h"
#include "relay_message.pb.h"
#include "tc_common_new/log.h"

using namespace tc;
using namespace relay;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}

TEST(Test_Hello, Hello) {
    std::thread t([]() {
        std::string device_id = "DID-11223344";
        auto ws_client = std::make_shared<RelayWsClient>("10.0.0.112",
                                                         40320,
                                                         device_id,
                                                         "Test-Device",
                                                         "Stream-101",
                                                         "ff785bd3031bc6cf920a782e50f43dcb");
        ws_client->Start();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (int i = 0; i < 10000; i++) {
            if (!ws_client->IsAlive()) {
                continue;
            }

            RelayMessage rl_msg;
            rl_msg.set_from_device_id(device_id);
            rl_msg.set_type(RelayMessageType::kRelayTargetMessage);
            auto relay = rl_msg.mutable_relay();
            relay->set_relay_msg_index(i);
            auto room_ids = relay->mutable_room_ids();
            room_ids->Add("test-room-id");
            //relay->set_payload(msg->AsString());

            ws_client->PostBinaryMessage(rl_msg.SerializeAsString());
            LOGI("Relay message index: {}", i);
        }

    });

    std::this_thread::sleep_for(std::chrono::seconds(5));
    t.join();
}