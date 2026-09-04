#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "modules/builtin_module_catalog.h"
#include "extensions/flow_node_plugin_registry.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/async_runtime.h"

namespace px::render {

using CompositionCompletion = std::function<void(ModuleLifecycleResult)>;

// Lifetime:
// - Owned by RdApplication in production and by the test fixture in isolation.
// - lifecycle_scope_ owns every lifecycle coroutine.
// - Coroutine factories capture weak_ptr; running coroutines promote it to a
//   shared owner for the duration of the operation.
// - RequestStop must complete before the final owner is released.
//
// Threading:
// - Lifecycle state is confined to lifecycle_scope_'s state-lane strand.
// - stop_requested_ is the only cross-thread mutable flag.
// - Catalog methods provide their own short critical sections.
// - No mutex or borrowed reference is held across co_await.
class RenderCompositionRoot final
    : public std::enable_shared_from_this<RenderCompositionRoot> {
public:
    static std::shared_ptr<RenderCompositionRoot> Create(
        const std::shared_ptr<PxAsyncRuntime>& runtime,
        const std::shared_ptr<BuiltinModuleCatalog>& catalog,
        std::shared_ptr<FlowNodePluginRegistry> flow_node_plugins = {});

    RenderCompositionRoot(std::shared_ptr<PxAsyncScope> lifecycle_scope,
                          std::shared_ptr<BuiltinModuleCatalog> catalog,
                          std::shared_ptr<FlowNodePluginRegistry> flow_node_plugins = {});
    ~RenderCompositionRoot();

    RenderCompositionRoot(const RenderCompositionRoot&) = delete;
    RenderCompositionRoot& operator=(const RenderCompositionRoot&) = delete;

    [[nodiscard]] ModuleLifecycleResult Register(
        BuiltinModuleRegistration registration);
    [[nodiscard]] ModuleLifecycleResult SetEnabled(const std::string& module_id,
                                                   bool enabled);
    [[nodiscard]] bool RequestStart(CompositionCompletion completion);
    [[nodiscard]] bool RequestStop(CompositionCompletion completion);
    [[nodiscard]] std::vector<BuiltinModuleSnapshot> SnapshotModules() const;
    [[nodiscard]] FlowNodeLifecycleResult RegisterFlowNodePlugin(FlowNodePluginRegistration registration);
    [[nodiscard]] FlowNodePluginCreateResult CreateFlowNodePlugin(const std::string& id) const;
    [[nodiscard]] std::vector<FlowNodeDescriptor> SnapshotFlowNodePlugins() const;

private:
    static PxAwaitable<void> RunStart(
        std::weak_ptr<RenderCompositionRoot> weak_owner,
        CompositionCompletion completion);
    static PxAwaitable<void> RunStop(
        std::weak_ptr<RenderCompositionRoot> weak_owner,
        CompositionCompletion completion);
    static PxAwaitable<std::optional<RenderError>> StopRegistrations(
        const std::shared_ptr<RenderCompositionRoot>& owner,
        std::vector<BuiltinModuleRegistration> registrations);

    std::shared_ptr<PxAsyncScope> lifecycle_scope_;
    std::shared_ptr<BuiltinModuleCatalog> catalog_;
    std::shared_ptr<FlowNodePluginRegistry> flow_node_plugins_;
    std::atomic_bool stop_requested_{false};

    // State-lane confined members.
    bool starting_{false};
    bool running_{false};
    bool stopping_{false};
    std::vector<BuiltinModuleRegistration> active_modules_;
    std::shared_ptr<PxAsyncOneShot<void>> start_completion_;
};

}  // namespace px::render
