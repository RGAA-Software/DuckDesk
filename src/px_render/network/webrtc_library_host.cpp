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

class WebRtcLibraryLease::State final {
public:
    State(std::string base_name,
          const WebRtcLibraryKind kind,
          std::shared_ptr<DynamicLibrary> library,
          std::shared_ptr<PxNetPlugin> compatibility_module)
        : base_name_(std::move(base_name)),
          kind_(kind),
          library_(std::move(library)),
          compatibility_module_(std::move(compatibility_module)) {}

    const std::string base_name_;
    const WebRtcLibraryKind kind_;
    const std::shared_ptr<DynamicLibrary> library_;
    const std::shared_ptr<PxNetPlugin> compatibility_module_;
};

WebRtcLibraryLease::WebRtcLibraryLease(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

WebRtcLibraryLease::~WebRtcLibraryLease() = default;

WebRtcLibraryKind WebRtcLibraryLease::Kind() const {
    return state_->kind_;
}

std::string WebRtcLibraryLease::BaseName() const {
    return state_->base_name_;
}

std::shared_ptr<PxNetPlugin>
WebRtcLibraryLease::CompatibilityModule() const {
    return state_->compatibility_module_;
}

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

std::vector<std::shared_ptr<WebRtcLibraryLease>> WebRtcLibraryHost::Load() {
    if (!loaded_libraries_.empty()) {
        return loaded_libraries_;
    }
    for (const auto& [base_name, kind] : {
             std::pair{std::string("net_rtc"), WebRtcLibraryKind::kRemote},
             std::pair{std::string("net_rtc_local"), WebRtcLibraryKind::kLocal}}) {
        if (auto library = LoadExact(base_name, kind)) {
            loaded_libraries_.push_back(std::move(library));
        }
    }
    return loaded_libraries_;
}

void WebRtcLibraryHost::Reset() {
    loaded_libraries_.clear();
}

std::shared_ptr<WebRtcLibraryLease> WebRtcLibraryHost::LoadExact(
    const std::string& base_name, const WebRtcLibraryKind kind) {
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
    auto compatibility_module = std::shared_ptr<PxNetPlugin>(
        instance,
        [library](PxNetPlugin*) noexcept { // NOLINT(gammaray-raw-pointer-boundary): DLL owns singleton; captured RAII library governs unload
            static_cast<void>(library);
        });
    LOGI("event=webrtc.library.load component=webrtc_library_host "
         "library={} outcome=success",
         base_name);
    auto state = std::make_shared<WebRtcLibraryLease::State>(
        base_name, kind, library, std::move(compatibility_module));
    return std::make_shared<WebRtcLibraryLease>(std::move(state));
}

}  // namespace px
