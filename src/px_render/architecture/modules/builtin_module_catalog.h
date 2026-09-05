#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "diagnostics/render_error.h"
#include "px_common/async_runtime.h"

namespace px::render {

enum class BuiltinModuleCapability {
    kSource,
    kProcessor,
    kObserver,
    kSink,
    kService,
    kNetwork,
};

enum class BuiltinModuleRuntimeState {
    kRegistered,
    kStarting,
    kRunning,
    kStopping,
    kStopped,
    kFailed,
};

struct BuiltinModuleDescriptor final {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string version_name;
    std::uint32_t version_code{0};
    BuiltinModuleCapability capability{BuiltinModuleCapability::kService};
    bool default_enabled{true};
    std::vector<std::string> dependencies;
};

using ModuleLifecycleResult = std::expected<void, RenderError>;
using ModuleLifecycleOperation =
    std::function<PxAwaitable<ModuleLifecycleResult>()>;
using ModuleEnableOperation = std::function<ModuleLifecycleResult(bool)>;

struct BuiltinModuleRegistration final {
    BuiltinModuleDescriptor descriptor;
    ModuleLifecycleOperation start;
    ModuleLifecycleOperation stop;
    ModuleEnableOperation set_enabled;
};

struct BuiltinModuleSnapshot final {
    BuiltinModuleDescriptor descriptor;
    BuiltinModuleRuntimeState runtime_state{
        BuiltinModuleRuntimeState::kRegistered};
    bool enabled{true};
    std::optional<RenderError> last_error;
};

// Lifetime:
// - Shared by RenderCompositionRoot and read-only control-plane reporters.
// - Registrations own value metadata and copyable callbacks; callbacks must
//   capture module implementations through weak_ptr.
//
// Threading:
// - Metadata and runtime snapshots are protected by mutex_.
// - Enable callbacks execute under control_mutex_, never under mutex_.
// - No lock is held across a coroutine suspension point.
class BuiltinModuleCatalog final {
public:
    static std::shared_ptr<BuiltinModuleCatalog> Create();

    BuiltinModuleCatalog() = default;
    BuiltinModuleCatalog(const BuiltinModuleCatalog&) = delete;
    BuiltinModuleCatalog& operator=(const BuiltinModuleCatalog&) = delete;

    [[nodiscard]] ModuleLifecycleResult Register(
        BuiltinModuleRegistration registration);
    [[nodiscard]] std::expected<std::vector<BuiltinModuleRegistration>,
                                RenderError>
    ResolveStartupPlan();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(const std::string& module_id,
                                                   bool enabled);
    [[nodiscard]] ModuleLifecycleResult SetRuntimeState(
        const std::string& module_id,
        BuiltinModuleRuntimeState state,
        std::optional<RenderError> error = std::nullopt);
    [[nodiscard]] std::vector<BuiltinModuleSnapshot> SnapshotAll() const;
    [[nodiscard]] std::optional<BuiltinModuleSnapshot> Snapshot(
        const std::string& module_id) const;

private:
    struct Entry final {
        BuiltinModuleRegistration registration;
        BuiltinModuleRuntimeState runtime_state{
            BuiltinModuleRuntimeState::kRegistered};
        bool enabled{true};
        std::optional<RenderError> last_error;
    };

    mutable std::mutex mutex_;
    std::mutex control_mutex_;
    std::vector<std::string> registration_order_;
    std::vector<std::shared_ptr<Entry>> entries_;
    bool sealed_{false};
};

[[nodiscard]] std::string_view StableModuleCapability(
    BuiltinModuleCapability capability) noexcept;
[[nodiscard]] std::string_view StableModuleRuntimeState(
    BuiltinModuleRuntimeState state) noexcept;

}  // namespace px::render
