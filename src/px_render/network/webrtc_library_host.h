#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace px {

class PxNetPlugin;
class RenderModuleRegistry;

enum class WebRtcLibraryKind {
    kRemote,
    kLocal,
};

// Concrete lifetime lease for one fixed WebRTC DLL. Product code observes the
// library identity and lifetime, not the legacy plug-in interface used inside
// the compatibility boundary.
class WebRtcLibraryLease final {
private:
    class State;

public:
    // Public only so make_shared can construct the lease; State is private and
    // can only be supplied by the fixed host.
    explicit WebRtcLibraryLease(std::shared_ptr<State> state);
    ~WebRtcLibraryLease();

    WebRtcLibraryLease(const WebRtcLibraryLease&) = delete;
    WebRtcLibraryLease& operator=(const WebRtcLibraryLease&) = delete;

    [[nodiscard]] WebRtcLibraryKind Kind() const;
    [[nodiscard]] std::string BaseName() const;

private:
    [[nodiscard]] std::shared_ptr<PxNetPlugin> CompatibilityModule() const;

    std::shared_ptr<State> state_;

    friend class RenderModuleRegistry;
    friend class WebRtcLibraryHost;
};

// Concrete compatibility host for the two libwebrtc-bearing Render DLLs.
// This is deliberately not an abstract transport interface and performs no
// directory discovery. It is the only owner of the WebRTC library lifetime.
//
// Lifetime:
// - Owned by the Render runtime module composition.
// - Returned concrete leases retain their DynamicLibrary owner.
// - Unload occurs only after Stop/Destroy and after all leases are released.
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

    [[nodiscard]] std::vector<std::shared_ptr<WebRtcLibraryLease>> Load();
    void Reset();

private:
    [[nodiscard]] std::shared_ptr<WebRtcLibraryLease> LoadExact(
        const std::string& base_name, WebRtcLibraryKind kind);

    std::filesystem::path library_directory_;
    std::vector<std::shared_ptr<WebRtcLibraryLease>> loaded_libraries_;
};

}  // namespace px
