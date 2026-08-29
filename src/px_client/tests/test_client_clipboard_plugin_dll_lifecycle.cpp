#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>

#include <QApplication>
#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_message.pb.h"
#include "px_client/plugin_interface/ct_plugin_context.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "px_client/plugin_interface/ct_plugin_interface.h"

namespace px {
namespace {

TEST(ClientClipboardPluginDllLifecycle,
     TenCreateQueueStopDestroyAndUnloadRounds) {
    const auto dll_path =
        std::filesystem::path(PX_CLIENT_CLIPBOARD_PLUGIN_DLL_PATH);
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
            parameters.cluster_["name"] = std::string("clipboard.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["screen_recording_path"] =
                std::filesystem::temp_directory_path().generic_string();
            parameters.cluster_["clipboard_enabled"] = false;
            parameters.cluster_["device_id"] =
                "clipboard-lifecycle-" + std::to_string(round);
            parameters.cluster_["stream_id"] =
                "clipboard-stream-" + std::to_string(round);
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["stream_name"] = std::string("clipboard-test");
            parameters.cluster_["display_name"] = std::string("clipboard-test");
            parameters.cluster_["display_remote_name"] =
                std::string("clipboard-remote");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            const auto callback_count = std::make_shared<std::atomic_int>(0);
            plugin.get().RegisterEventCallback(
                [callback_count](
                    const std::shared_ptr<ClientPluginBaseEvent>&) {
                    callback_count->fetch_add(1);
                });

            for (int index = 0; index < 64; ++index) {
                auto message = std::make_shared<Message>();
                message->set_type(MessageType::kClipboardReqAtBegin);
                message->mutable_cp_req_at_begin()->set_full_name(
                    "clipboard-queued-" + std::to_string(index));
                plugin.get().OnMessage(std::move(message));
            }

            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            const auto stopped_barrier = std::make_shared<std::promise<void>>();
            auto stopped_barrier_result = stopped_barrier->get_future();
            plugin.get().GetPluginContext()->PostWorkTask(
                [stopped_barrier]() { stopped_barrier->set_value(); });
            stopped_barrier_result.get();
            const auto callbacks_at_stop = callback_count->load();

            for (int index = 0; index < 64; ++index) {
                auto message = std::make_shared<Message>();
                message->set_type(MessageType::kClipboardReqAtEnd);
                auto request = message->mutable_cp_req_at_end();
                request->set_full_name(
                    "clipboard-stopped-" + std::to_string(index));
                request->set_success(true);
                plugin.get().OnMessage(std::move(message));
            }

            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_EQ(callback_count->load(), callbacks_at_stop)
                << "callbacks escaped the stopped event channel in round "
                << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"clipboard.dll"), nullptr)
            << "clipboard module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
