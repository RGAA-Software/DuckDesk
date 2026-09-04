#pragma once

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "extensions/flow_node_plugin.h"

namespace px::render {

using FlowNodePluginCreateResult = std::expected<std::shared_ptr<FlowNodePlugin>, RenderError>;

// Process-local registry for replaceable workflow nodes. Registration is explicit at the composition root; there is no static
// self-registration or generic plug-in service locator. Factories execute outside the registry lock.
class FlowNodePluginRegistry final {
  public:
    static std::shared_ptr<FlowNodePluginRegistry> Create();

    FlowNodePluginRegistry() = default;
    FlowNodePluginRegistry(const FlowNodePluginRegistry&) = delete;
    FlowNodePluginRegistry& operator=(const FlowNodePluginRegistry&) = delete;

    [[nodiscard]] FlowNodeLifecycleResult Register(FlowNodePluginRegistration registration);
    [[nodiscard]] FlowNodePluginCreateResult CreateNode(const std::string& id) const;
    [[nodiscard]] std::vector<FlowNodeDescriptor> SnapshotDescriptors() const;
    [[nodiscard]] std::vector<FlowNodeDescriptor> SnapshotDescriptors(FlowNodeRole role) const;

  private:
    mutable std::mutex mutex_{};
    std::vector<FlowNodePluginRegistration> registrations_{};
};

[[nodiscard]] std::string_view StableFlowNodeRole(FlowNodeRole role) noexcept;

} // namespace px::render
