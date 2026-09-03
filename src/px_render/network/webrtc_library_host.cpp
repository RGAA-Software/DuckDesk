#include "network/webrtc_library_host.h"

#include <utility>

#include "px_common_new/log.h"
#include "px_common_new/string_util.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px {
namespace {

using WebRtcFactory = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary): established WebRTC DLL factory ABI

}  // namespace

std::shared_ptr<WebRtcLibraryHost> WebRtcLibraryHost::Create(
    std::filesystem::path library_directory) {
    return std::make_shared<WebRtcLibraryHost>(
        std::move(library_directory));
}

WebRtcLibraryHost::WebRtcLibraryHost(
    std::filesystem::path library_directory)
    : library_directory_(std::move(library_directory)) {}

WebRtcLibraryHost::~WebRtcLibraryHost() {
    Reset();
}

std::vector<std::shared_ptr<PxNetPlugin>> WebRtcLibraryHost::Load() {
    if (!loaded_modules_.empty()) {
        return loaded_modules_;
    }
    for (const auto& base_name : {std::string("net_rtc"),
                                  std::string("net_rtc_local")}) {
        if (auto module = LoadExact(base_name)) {
            loaded_modules_.push_back(std::move(module));
        }
    }
    return loaded_modules_;
}

void WebRtcLibraryHost::Reset() {
    loaded_modules_.clear();
}

std::shared_ptr<PxNetPlugin> WebRtcLibraryHost::LoadExact(
    const std::string& base_name) {
#if defined(_WIN32)
    const auto path = library_directory_ / (base_name + ".dll");
#else
    const auto path = library_directory_ / (base_name + ".so");
#endif
    if (!std::filesystem::is_regular_file(path)) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "library={} outcome=failed reason=missing_file",
             base_name);
        return {};
    }
    auto library = std::make_shared<DynamicLibrary>(path.wstring());
    if (!library->Load()) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "library={} outcome=failed reason={}",
             base_name, library->GetErrorString());
        return {};
    }
    const auto factory = reinterpret_cast<WebRtcFactory>(
        library->GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary): established WebRTC DLL symbol ABI
    if (!factory) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "library={} outcome=failed reason=missing_factory",
             base_name);
        return {};
    }
    auto instance = static_cast<PxNetPlugin*>(factory()); // NOLINT(gammaray-raw-pointer-boundary): DLL singleton is borrowed and immediately lifetime-aliased
    if (!instance) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "library={} outcome=failed reason=null_instance",
             base_name);
        return {};
    }
    auto module = std::shared_ptr<PxNetPlugin>(
        instance,
        [library](PxNetPlugin*) noexcept { // NOLINT(gammaray-raw-pointer-boundary): DLL owns singleton; captured RAII library governs unload
            static_cast<void>(library);
        });
    LOGI("event=webrtc.library.load component=webrtc_library_host "
         "library={} outcome=success",
         base_name);
    return module;
}

}  // namespace px
