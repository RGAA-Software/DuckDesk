#include <Windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <QApplication>
#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_message.pb.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "px_client/plugin_interface/ct_plugin_interface.h"

namespace px {
namespace {

TEST(ClientFtPluginDllLifecycle, TenCreateStopDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_CLIENT_FT_PLUGIN_DLL_PATH);
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
            auto plugin = std::ref(*static_cast<ClientPluginInterface*>(
                get_instance()));  // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI

            ClientPluginParam parameters;
            parameters.cluster_["name"] = std::string("ft.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["screen_recording_path"] =
                std::filesystem::temp_directory_path().generic_string();
            parameters.cluster_["clipboard_enabled"] = false;
            parameters.cluster_["device_id"] =
                "ft-lifecycle-" + std::to_string(round);
            parameters.cluster_["stream_id"] =
                "ft-stream-" + std::to_string(round);
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["stream_name"] = std::string("ft-test");
            parameters.cluster_["display_name"] = std::string("ft-test");
            parameters.cluster_["display_remote_name"] =
                std::string("ft-remote");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<ClientPluginBaseEvent>& event) {
                    if (const auto network =
                            std::dynamic_pointer_cast<ClientPluginNetworkEvent>(event)) {
                        network->send_result_ = FileTransferSendResult::Accepted();
                    }
                });
            EXPECT_FALSE(plugin.get().HasProcessingTasks()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"ft.dll"), nullptr)
            << "FT module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
