#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_message.pb.h"
#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {
namespace {

class ScopedTestDirectory final {
public:
    ScopedTestDirectory()
        : path_(std::filesystem::temp_directory_path()
                / ("gammaray_ft_plugin_lifecycle_"
                   + std::to_string(GetCurrentProcessId()))) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "create lifecycle test directory", path_, error);
        }
        for (int index = 0; index < 128; ++index) {
            std::ofstream file(path_ / ("queued_" + std::to_string(index) + ".bin"),
                               std::ios::binary);
            file << "queued callback lifecycle payload " << index;
        }
    }

    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::shared_ptr<Message> NewReadDirectoryMessage(
    const std::filesystem::path& path, int sequence) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kFileAction);
    message->set_stream_id("ft-dll-lifecycle-" + std::to_string(sequence));
    message->set_device_id("ft-dll-lifecycle-test");
    auto& read_directory = *message->mutable_file_action()->mutable_read_dir();
    read_directory.set_path(path.generic_string());
    read_directory.set_include_hidden(true);
    return message;
}

TEST(FtPluginDllLifecycle, QueuedCallbacksDrainBeforeRealUnload) {
    const ScopedTestDirectory test_directory;
    const auto dll_path = std::filesystem::path(PX_FT_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    {
        DynamicLibrary library(dll_path.wstring());
        ASSERT_TRUE(library.Load()) << library.GetErrorString();

        using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary): established plug-in ABI.
        const auto get_instance = reinterpret_cast<GetInstanceFn>(
            library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary): transient plug-in symbol.
        ASSERT_NE(get_instance, nullptr);
        auto plugin = std::ref(
            *static_cast<PxPluginInterface*>(get_instance())); // NOLINT(gammaray-raw-pointer-boundary): established plug-in instance ABI.

        PxPluginParam parameters;
        parameters.cluster_["name"] = std::string("ft.dll");
        parameters.cluster_["base_path"] = dll_path.parent_path().generic_string();
        parameters.cluster_["base_data_path"] = test_directory.path().wstring();
        parameters.cluster_["device_id"] = std::string("ft-dll-lifecycle-test");
        parameters.cluster_["relay_enabled"] = false;
        parameters.cluster_["language"] = int64_t{0};
        parameters.cluster_["appkey"] = std::string();

        ASSERT_TRUE(plugin.get().OnCreate(parameters));
        PxPluginSettingsInfo settings;
        settings.file_transfer_enabled_ = true;
        plugin.get().OnSyncPluginSettingsInfo(settings);

        for (int sequence = 0; sequence < 64; ++sequence) {
            plugin.get().OnMessage(
                NewReadDirectoryMessage(test_directory.path(), sequence));
        }

        const auto stop_started = std::chrono::steady_clock::now();
        EXPECT_TRUE(plugin.get().OnStop());
        EXPECT_TRUE(plugin.get().OnDestroy());
        const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
        EXPECT_LT(stop_elapsed, std::chrono::seconds(10));
    }

    EXPECT_EQ(GetModuleHandleW(L"ft.dll"), nullptr)
        << "FT module remained loaded after OnStop/OnDestroy and library release";
}

} // namespace
} // namespace px
