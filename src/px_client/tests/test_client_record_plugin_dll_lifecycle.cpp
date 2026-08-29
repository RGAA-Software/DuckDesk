#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <QApplication>
#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_message.pb.h"
#include "px_client/plugin_interface/ct_media_record_plugin_interface.h"
#include "px_client/plugin_interface/ct_plugin_events.h"

namespace px {
namespace {

TEST(ClientRecordPluginDllLifecycle, TenCreateQueueDestroyAndUnloadRounds) {
    const auto dll_path =
        std::filesystem::path(PX_CLIENT_RECORD_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();
            using GetInstanceFn = void* (*)();  // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance"));  // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(*static_cast<MediaRecordPluginClientInterface*>(
                get_instance()));  // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI

            ClientPluginParam parameters;
            parameters.cluster_["name"] = std::string("record.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["screen_recording_path"] =
                std::filesystem::temp_directory_path().generic_string();
            parameters.cluster_["clipboard_enabled"] = false;
            parameters.cluster_["device_id"] =
                "record-lifecycle-" + std::to_string(round);
            parameters.cluster_["stream_id"] =
                "record-stream-" + std::to_string(round);
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["stream_name"] = std::string("record-test");
            parameters.cluster_["display_name"] = std::string("record-test");
            parameters.cluster_["display_remote_name"] =
                std::string("record-remote");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            const auto callback_count = std::make_shared<std::atomic_int>(0);
            plugin.get().RegisterEventCallback(
                [callback_count](
                    const std::shared_ptr<ClientPluginBaseEvent>&) {
                    callback_count->fetch_add(1);
                });
            plugin.get().StartRecord();
            for (int index = 0; index < 64; ++index) {
                auto message = std::make_shared<Message>();
                message->set_type(MessageType::kAudioFrame);
                plugin.get().OnMessage(std::move(message));
            }
            plugin.get().EndRecord();
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_EQ(callback_count->load(), 0) << "round " << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"record.dll"), nullptr)
            << "record module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
