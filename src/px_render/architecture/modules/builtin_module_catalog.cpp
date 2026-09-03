#include "modules/builtin_module_catalog.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace px::render {
namespace {

RenderError MakeCatalogError(const RenderErrorCode code,
                             std::string operation,
                             std::string reason) {
    return RenderError{
        .code = code,
        .component = "builtin_module_catalog",
        .operation = std::move(operation),
        .stage = "module_control",
        .reason = std::move(reason),
        .recoverable = false,
    };
}

}  // namespace

std::shared_ptr<BuiltinModuleCatalog> BuiltinModuleCatalog::Create() {
    return std::make_shared<BuiltinModuleCatalog>();
}

ModuleLifecycleResult BuiltinModuleCatalog::Register(
    BuiltinModuleRegistration registration) {
    if (registration.descriptor.id.empty() ||
        registration.descriptor.name.empty() || !registration.start ||
        !registration.stop || !registration.set_enabled) {
        return std::unexpected(MakeCatalogError(
            RenderErrorCode::kModuleInvalidDescriptor,
            "register",
            "module id, name, and lifecycle operations are required"));
    }
    if (std::ranges::find(registration.descriptor.dependencies,
                          registration.descriptor.id) !=
        registration.descriptor.dependencies.end()) {
        return std::unexpected(MakeCatalogError(
            RenderErrorCode::kModuleDependencyCycle,
            "register",
            "module cannot depend on itself"));
    }

    std::lock_guard lock(mutex_);
    if (sealed_) {
        return std::unexpected(MakeCatalogError(
            RenderErrorCode::kModuleLifecycleRejected,
            "register",
            "catalog is sealed after lifecycle startup planning"));
    }
    const auto duplicate = std::ranges::find_if(
        entries_, [&registration](const std::shared_ptr<Entry>& entry) {
            return entry->registration.descriptor.id ==
                   registration.descriptor.id;
        });
    if (duplicate != entries_.end()) {
        return std::unexpected(MakeCatalogError(
            RenderErrorCode::kModuleAlreadyRegistered,
            "register",
            "module id is already registered"));
    }

    const auto module_id = registration.descriptor.id;
    const auto enabled = registration.descriptor.default_enabled;
    entries_.push_back(std::make_shared<Entry>(Entry{
        .registration = std::move(registration),
        .enabled = enabled,
    }));
    registration_order_.push_back(module_id);
    return {};
}

std::expected<std::vector<BuiltinModuleRegistration>, RenderError>
BuiltinModuleCatalog::ResolveStartupPlan() {
    std::lock_guard lock(mutex_);
    sealed_ = true;

    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_by_id;
    entries_by_id.reserve(entries_.size());
    for (const auto& entry : entries_) {
        entries_by_id.emplace(entry->registration.descriptor.id, entry);
    }

    enum class VisitState { kVisiting, kVisited };
    std::unordered_map<std::string, VisitState> visit_states;
    std::vector<BuiltinModuleRegistration> plan;
    plan.reserve(entries_.size());
    std::optional<RenderError> plan_error;

    std::function<bool(const std::string&)> visit =
        [&](const std::string& module_id) {
            const auto state = visit_states.find(module_id);
            if (state != visit_states.end()) {
                if (state->second == VisitState::kVisiting) {
                    plan_error = MakeCatalogError(
                        RenderErrorCode::kModuleDependencyCycle,
                        "resolve_startup_plan",
                        "module dependency graph contains a cycle at " +
                            module_id);
                    return false;
                }
                return true;
            }

            const auto entry = entries_by_id.find(module_id);
            if (entry == entries_by_id.end()) {
                plan_error = MakeCatalogError(
                    RenderErrorCode::kModuleDependencyUnavailable,
                    "resolve_startup_plan",
                    "module dependency is not registered: " + module_id);
                return false;
            }
            visit_states.emplace(module_id, VisitState::kVisiting);
            for (const auto& dependency :
                 entry->second->registration.descriptor.dependencies) {
                if (!visit(dependency)) {
                    return false;
                }
            }
            visit_states[module_id] = VisitState::kVisited;
            plan.push_back(entry->second->registration);
            return true;
        };

    for (const auto& module_id : registration_order_) {
        if (!visit(module_id)) {
            return std::unexpected(std::move(*plan_error));
        }
    }
    return plan;
}

ModuleLifecycleResult BuiltinModuleCatalog::SetEnabled(
    const std::string& module_id,
    const bool enabled) {
    std::lock_guard control_lock(control_mutex_);
    ModuleEnableOperation operation;
    {
        std::lock_guard lock(mutex_);
        const auto entry = std::ranges::find_if(
            entries_, [&module_id](const std::shared_ptr<Entry>& candidate) {
                return candidate->registration.descriptor.id == module_id;
            });
        if (entry == entries_.end()) {
            return std::unexpected(MakeCatalogError(
                RenderErrorCode::kModuleNotFound,
                "set_enabled",
                "module id is not registered"));
        }
        if ((*entry)->enabled == enabled) {
            return {};
        }
        operation = (*entry)->registration.set_enabled;
    }

    auto result = operation(enabled);
    if (!result) {
        return result;
    }
    {
        std::lock_guard lock(mutex_);
        const auto entry = std::ranges::find_if(
            entries_, [&module_id](const std::shared_ptr<Entry>& candidate) {
                return candidate->registration.descriptor.id == module_id;
            });
        if (entry == entries_.end()) {
            return std::unexpected(MakeCatalogError(
                RenderErrorCode::kModuleNotFound,
                "set_enabled",
                "module disappeared during enable transition"));
        }
        (*entry)->enabled = enabled;
        (*entry)->last_error.reset();
    }
    return {};
}

ModuleLifecycleResult BuiltinModuleCatalog::SetRuntimeState(
    const std::string& module_id,
    const BuiltinModuleRuntimeState state,
    std::optional<RenderError> error) {
    std::lock_guard lock(mutex_);
    const auto entry = std::ranges::find_if(
        entries_, [&module_id](const std::shared_ptr<Entry>& candidate) {
            return candidate->registration.descriptor.id == module_id;
        });
    if (entry == entries_.end()) {
        return std::unexpected(MakeCatalogError(
            RenderErrorCode::kModuleNotFound,
            "set_runtime_state",
            "module id is not registered"));
    }
    (*entry)->runtime_state = state;
    (*entry)->last_error = std::move(error);
    return {};
}

std::vector<BuiltinModuleSnapshot> BuiltinModuleCatalog::SnapshotAll() const {
    std::lock_guard lock(mutex_);
    std::vector<BuiltinModuleSnapshot> snapshots;
    snapshots.reserve(entries_.size());
    for (const auto& entry : entries_) {
        snapshots.push_back(BuiltinModuleSnapshot{
            .descriptor = entry->registration.descriptor,
            .runtime_state = entry->runtime_state,
            .enabled = entry->enabled,
            .last_error = entry->last_error,
        });
    }
    return snapshots;
}

std::optional<BuiltinModuleSnapshot> BuiltinModuleCatalog::Snapshot(
    const std::string& module_id) const {
    std::lock_guard lock(mutex_);
    const auto entry = std::ranges::find_if(
        entries_, [&module_id](const std::shared_ptr<Entry>& candidate) {
            return candidate->registration.descriptor.id == module_id;
        });
    if (entry == entries_.end()) {
        return std::nullopt;
    }
    return BuiltinModuleSnapshot{
        .descriptor = (*entry)->registration.descriptor,
        .runtime_state = (*entry)->runtime_state,
        .enabled = (*entry)->enabled,
        .last_error = (*entry)->last_error,
    };
}

std::string_view StableModuleCapability(
    const BuiltinModuleCapability capability) noexcept {
    switch (capability) {
        case BuiltinModuleCapability::kSource:
            return "source";
        case BuiltinModuleCapability::kProcessor:
            return "processor";
        case BuiltinModuleCapability::kObserver:
            return "observer";
        case BuiltinModuleCapability::kSink:
            return "sink";
        case BuiltinModuleCapability::kService:
            return "service";
        case BuiltinModuleCapability::kNetwork:
            return "network";
    }
    return "service";
}

std::string_view StableModuleRuntimeState(
    const BuiltinModuleRuntimeState state) noexcept {
    switch (state) {
        case BuiltinModuleRuntimeState::kRegistered:
            return "registered";
        case BuiltinModuleRuntimeState::kStarting:
            return "starting";
        case BuiltinModuleRuntimeState::kRunning:
            return "running";
        case BuiltinModuleRuntimeState::kStopping:
            return "stopping";
        case BuiltinModuleRuntimeState::kStopped:
            return "stopped";
        case BuiltinModuleRuntimeState::kFailed:
            return "failed";
    }
    return "failed";
}

}  // namespace px::render
