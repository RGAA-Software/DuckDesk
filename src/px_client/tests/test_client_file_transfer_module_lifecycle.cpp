#include <memory>

#include <QApplication>
#include <QFile>
#include <gtest/gtest.h>

#include "fake_client_module_services.h"
#include "px_client/modules/file_transfer/file_transfer_module.h"
#include "px_message.pb.h"

namespace px {
namespace {

TEST(ClientFileTransferModuleLifecycle, RepeatedStartStopAndReconnect) {
    const auto services =
        std::make_shared<test::FakeClientModuleServices>();
    for (int round = 0; round < 10; ++round) {
        const auto module = std::make_shared<ClientFileTransferModule>(
            std::weak_ptr<ClientModuleServices>(services));
        ASSERT_TRUE(module->Start(test::MakeModuleConfig(
            "file-transfer-module-" + std::to_string(round))));
        ASSERT_TRUE(QFile::exists(":/ft/icons/ic_refresh.svg"));
        for (int index = 0; index < 64; ++index) {
            auto message = std::make_shared<Message>();
            message->set_type(MessageType::kFileResponse);
            auto& directory =
                *message->mutable_file_response()->mutable_dir();
            directory.set_id(index);
            directory.set_path("/");
            module->HandleMessage(message);
        }
        module->OnTransportConnected();
        EXPECT_FALSE(module->HasProcessingTasks());
        module->Stop();
        module->Stop();

        auto late_message = std::make_shared<Message>();
        late_message->set_type(MessageType::kFileResponse);
        module->HandleMessage(late_message);
        EXPECT_FALSE(module->HasProcessingTasks());
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
