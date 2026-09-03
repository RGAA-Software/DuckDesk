#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace px {

class PxNetPlugin;

// Concrete compatibility host for the two libwebrtc-bearing Render DLLs.
// This is deliberately not an abstract transport interface and performs no
// directory discovery. It is the only owner of the WebRTC library lifetime.
//
// Lifetime:
// - Owned by the Render runtime module composition.
// - Returned module aliases retain their DynamicLibrary owner.
// - Unload occurs only after Stop/Destroy and after all aliases are released.
//
// Threading:
// - Load and Reset are lifecycle-thread operations and are not concurrent.
class WebRtcLibraryHost final {
public:
    [[nodiscard]] static std::shared_ptr<WebRtcLibraryHost> Create(
        std::filesystem::path library_directory);

    explicit WebRtcLibraryHost(std::filesystem::path library_directory);
    ~WebRtcLibraryHost();

    WebRtcLibraryHost(const WebRtcLibraryHost&) = delete;
    WebRtcLibraryHost& operator=(const WebRtcLibraryHost&) = delete;

    [[nodiscard]] std::vector<std::shared_ptr<PxNetPlugin>> Load();
    void Reset();

private:
    [[nodiscard]] std::shared_ptr<PxNetPlugin> LoadExact(
        const std::string& base_name);

    std::filesystem::path library_directory_;
    std::vector<std::shared_ptr<PxNetPlugin>> loaded_modules_;
};

}  // namespace px
