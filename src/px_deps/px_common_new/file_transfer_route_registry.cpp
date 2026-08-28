#include "file_transfer_route_registry.h"

namespace px {

FileTransferRoute FileTransferRouteRegistry::Bind(const std::string& stream_id,
                                                  const std::string& plugin_id,
                                                  const std::string& connection_instance_id) {
    std::lock_guard lock(mutex_);
    if (const auto current = routes_.find(stream_id);
        current != routes_.end() && current->second.plugin_id == plugin_id &&
        current->second.connection_instance_id == connection_instance_id) {
        return current->second;
    }
    FileTransferRoute route{plugin_id, connection_instance_id, next_generation_++};
    routes_[stream_id] = route;
    return route;
}

std::optional<FileTransferRoute> FileTransferRouteRegistry::Resolve(
    const std::string& stream_id) const {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(stream_id);
    if (route == routes_.end()) {
        return std::nullopt;
    }
    return route->second;
}

bool FileTransferRouteRegistry::Remove(const std::string& stream_id) {
    std::lock_guard lock(mutex_);
    return routes_.erase(stream_id) > 0;
}

bool FileTransferRouteRegistry::RemoveIfGenerationMatches(
    const std::string& stream_id,
    std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(stream_id);
    if (route == routes_.end() || route->second.generation != generation) {
        return false;
    }
    routes_.erase(route);
    return true;
}

bool FileTransferRouteRegistry::RemoveIfPluginMatches(
    const std::string& stream_id,
    const std::string& plugin_id) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(stream_id);
    if (route == routes_.end() || route->second.plugin_id != plugin_id) {
        return false;
    }
    routes_.erase(route);
    return true;
}

bool FileTransferRouteRegistry::RemoveIfConnectionMatches(
    const std::string& stream_id,
    const std::string& plugin_id,
    const std::string& connection_instance_id) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(stream_id);
    if (route == routes_.end() || route->second.plugin_id != plugin_id ||
        route->second.connection_instance_id != connection_instance_id) {
        return false;
    }
    routes_.erase(route);
    return true;
}

void FileTransferRouteRegistry::Clear() {
    std::lock_guard lock(mutex_);
    routes_.clear();
}

std::size_t FileTransferRouteRegistry::Size() const {
    std::lock_guard lock(mutex_);
    return routes_.size();
}

} // namespace px
