#include "extensions/flow_node_plugin_registry.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace px::render {
namespace {

RenderError MakeRegistryError(const RenderErrorCode code, std::string operation, std::string reason) {
    return RenderError{
        .code = code,
        .component = "flow_node_plugin_registry",
        .operation = std::move(operation),
        .stage = "flow_node_registration",
        .reason = std::move(reason),
        .recoverable = false,
    };
}

bool ImplementsDeclaredRole(const std::shared_ptr<FlowNodePlugin>& node, const FlowNodeRole role) {
    switch (role) {
    case FlowNodeRole::kVideoSource:
        return static_cast<bool>(std::dynamic_pointer_cast<VideoSourcePlugin>(node));
    case FlowNodeRole::kAudioSource:
        return static_cast<bool>(std::dynamic_pointer_cast<AudioSourcePlugin>(node));
    case FlowNodeRole::kVideoProcessor:
        return static_cast<bool>(std::dynamic_pointer_cast<VideoProcessorPlugin>(node));
    case FlowNodeRole::kAudioProcessor:
        return static_cast<bool>(std::dynamic_pointer_cast<AudioProcessorPlugin>(node));
    case FlowNodeRole::kVideoEncoder:
        return static_cast<bool>(std::dynamic_pointer_cast<VideoEncoderPlugin>(node));
    case FlowNodeRole::kAudioEncoder:
        return static_cast<bool>(std::dynamic_pointer_cast<AudioEncoderPlugin>(node));
    case FlowNodeRole::kObserver:
        return static_cast<bool>(std::dynamic_pointer_cast<ObserverPlugin>(node));
    case FlowNodeRole::kSink:
        return static_cast<bool>(std::dynamic_pointer_cast<SinkPlugin>(node));
    }
    return false;
}

} // namespace

std::shared_ptr<FlowNodePluginRegistry> FlowNodePluginRegistry::Create() {
    return std::make_shared<FlowNodePluginRegistry>();
}

FlowNodeLifecycleResult FlowNodePluginRegistry::Register(FlowNodePluginRegistration registration) {
    if (registration.descriptor.id.empty() || registration.descriptor.name.empty() || !registration.create) {
        return std::unexpected(
            MakeRegistryError(RenderErrorCode::kModuleInvalidDescriptor, "register", "flow node id, name, and factory are required"));
    }
    if (std::ranges::find(registration.dependencies, registration.descriptor.id) != registration.dependencies.end()) {
        return std::unexpected(MakeRegistryError(RenderErrorCode::kModuleDependencyCycle, "register", "flow node cannot depend on itself"));
    }

    std::lock_guard lock(mutex_);
    const auto duplicate = std::ranges::find_if(registrations_, [&registration](const FlowNodePluginRegistration& candidate) {
        return candidate.descriptor.id == registration.descriptor.id;
    });
    if (duplicate != registrations_.end()) {
        return std::unexpected(MakeRegistryError(RenderErrorCode::kModuleAlreadyRegistered, "register", "flow node id is already registered"));
    }
    registrations_.push_back(std::move(registration));
    return {};
}

FlowNodePluginCreateResult FlowNodePluginRegistry::CreateNode(const std::string& id) const {
    FlowNodePluginFactory factory;
    FlowNodeDescriptor registered_descriptor;
    {
        std::lock_guard lock(mutex_);
        const auto registration =
            std::ranges::find_if(registrations_, [&id](const FlowNodePluginRegistration& candidate) { return candidate.descriptor.id == id; });
        if (registration == registrations_.end()) {
            return std::unexpected(MakeRegistryError(RenderErrorCode::kModuleNotFound, "create", "flow node id is not registered"));
        }
        factory = registration->create;
        registered_descriptor = registration->descriptor;
    }

    std::shared_ptr<FlowNodePlugin> node;
    try {
        node = factory();
    } catch (const std::exception& error) {
        return std::unexpected(MakeRegistryError(RenderErrorCode::kModuleLifecycleRejected, "create",
                                                 "flow node factory threw an exception: " + std::string(error.what())));
    } catch (...) {
        return std::unexpected(
            MakeRegistryError(RenderErrorCode::kModuleLifecycleRejected, "create", "flow node factory threw an unknown exception"));
    }

    if (!node) {
        return std::unexpected(MakeRegistryError(RenderErrorCode::kModuleLifecycleRejected, "create", "flow node factory returned no instance"));
    }
    const auto& actual_descriptor = node->Descriptor();
    if (actual_descriptor.id != registered_descriptor.id || actual_descriptor.role != registered_descriptor.role ||
        !ImplementsDeclaredRole(node, registered_descriptor.role)) {
        return std::unexpected(
            MakeRegistryError(RenderErrorCode::kModuleInvalidDescriptor, "create", "flow node instance does not match its registered id and role"));
    }
    return node;
}

std::vector<FlowNodeDescriptor> FlowNodePluginRegistry::SnapshotDescriptors() const {
    std::lock_guard lock(mutex_);
    std::vector<FlowNodeDescriptor> descriptors;
    descriptors.reserve(registrations_.size());
    for (const auto& registration : registrations_) {
        descriptors.push_back(registration.descriptor);
    }
    return descriptors;
}

std::vector<FlowNodeDescriptor> FlowNodePluginRegistry::SnapshotDescriptors(const FlowNodeRole role) const {
    std::lock_guard lock(mutex_);
    std::vector<FlowNodeDescriptor> descriptors;
    for (const auto& registration : registrations_) {
        if (registration.descriptor.role == role) {
            descriptors.push_back(registration.descriptor);
        }
    }
    return descriptors;
}

std::string_view StableFlowNodeRole(const FlowNodeRole role) noexcept {
    switch (role) {
    case FlowNodeRole::kVideoSource:
        return "video_source";
    case FlowNodeRole::kAudioSource:
        return "audio_source";
    case FlowNodeRole::kVideoProcessor:
        return "video_processor";
    case FlowNodeRole::kAudioProcessor:
        return "audio_processor";
    case FlowNodeRole::kVideoEncoder:
        return "video_encoder";
    case FlowNodeRole::kAudioEncoder:
        return "audio_encoder";
    case FlowNodeRole::kObserver:
        return "observer";
    case FlowNodeRole::kSink:
        return "sink";
    }
    return "observer";
}

} // namespace px::render
