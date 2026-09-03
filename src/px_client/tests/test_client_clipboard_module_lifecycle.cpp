#include <memory>

#include <QApplication>
#include <gtest/gtest.h>

#include "fake_client_module_services.h"
#include "px_client/modules/clipboard/clipboard_module.h"
#include "px_message.pb.h"

namespace px {
namespace {

TEST(ClientClipboardModuleLifecycle, RepeatedStartQueueStopRejectsLateWork) {
    const auto services =
        std::make_shared<test::FakeClientModuleServices>();
    for (int round = 0; round < 10; ++round) {
        const auto module = std::make_shared<ClientClipboardModule>(
            std::weak_ptr<ClientModuleServices>(services));
        ASSERT_TRUE(module->Start(test::MakeModuleConfig(
            "clipboard-module-" + std::to_string(round))));
        for (int index = 0; index < 64; ++index) {
            auto message = std::make_shared<Message>();
            message->set_type(MessageType::kClipboardReqAtBegin);
            message->mutable_cp_req_at_begin()->set_full_name(
                "clipboard-queued-" + std::to_string(index));
            module->HandleMessage(message);
        }
        module->Stop();
        module->Stop();
        const auto begins_at_stop = services->transfer_begins_.load();

        auto late_message = std::make_shared<Message>();
        late_message->set_type(MessageType::kClipboardReqAtEnd);
        late_message->mutable_cp_req_at_end()->set_full_name("late");
        late_message->mutable_cp_req_at_end()->set_success(true);
        module->HandleMessage(late_message);
        EXPECT_EQ(services->transfer_begins_.load(), begins_at_stop);
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
