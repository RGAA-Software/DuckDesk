#include "file_transfer_route_registry.h"

namespace px {

std::string FileTransferRouteRegistry::MakeRouteKey(const std::string& logical_session_id,
                                                     const std::string& stream_id) {
    return logical_session_id + "\x1f" + stream_id;
}

FileTransferRoute FileTransferRouteRegistry::Bind(const std::string& logical_session_id,
                                                  const std::string& stream_id,
                                                  const std::string& plugin_id,
                                                  const std::string& connection_instance_id) {
    std::lock_guard lock(mutex_);
    const auto route_key = MakeRouteKey(logical_session_id, stream_id);
    if (const auto current = routes_.find(route_key);
        current != routes_.end() && current->second.plugin_id == plugin_id &&
        current->second.connection_instance_id == connection_instance_id) {
        return current->second;
    }
    FileTransferRoute route{logical_session_id, plugin_id, connection_instance_id, next_generation_++};
    routes_[route_key] = route;
    return route;
}

FileTransferRoute FileTransferRouteRegistry::Bind(const std::string& stream_id,
                                                  const std::string& plugin_id,
                                                  const std::string& connection_instance_id) {
    return Bind({}, stream_id, plugin_id, connection_instance_id);
}

std::optional<FileTransferRoute> FileTransferRouteRegistry::Resolve(
    const std::string& logical_session_id, const std::string& stream_id) const {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(MakeRouteKey(logical_session_id, stream_id));
    return route == routes_.end() ? std::nullopt
                                  : std::optional<FileTransferRoute>(route->second);
}

std::optional<FileTransferRoute> FileTransferRouteRegistry::Resolve(
    const std::string& stream_id) const {
    return Resolve({}, stream_id);
}

std::optional<FileTransferRoute> FileTransferRouteRegistry::ResolveUniqueStream(
    const std::string& stream_id) const {
    std::lock_guard lock(mutex_);
    std::optional<FileTransferRoute> found;
    for (const auto& [key, route] : routes_) {
        static_cast<void>(key);
        if (route.logical_session_id.empty()) {
            continue;
        }
        if (key.size() <= stream_id.size()
            || key.compare(key.size() - stream_id.size(), stream_id.size(), stream_id) != 0
            || key[key.size() - stream_id.size() - 1] != '\x1f') {
            continue;
        }
        if (found.has_value()) {
            return std::nullopt;
        }
        found = route;
    }
    return found;
}

bool FileTransferRouteRegistry::Remove(const std::string& logical_session_id,
                                       const std::string& stream_id) {
    std::lock_guard lock(mutex_);
    return routes_.erase(MakeRouteKey(logical_session_id, stream_id)) > 0;
}

bool FileTransferRouteRegistry::Remove(const std::string& stream_id) {
    return Remove({}, stream_id);
}

bool FileTransferRouteRegistry::RemoveIfGenerationMatches(
    const std::string& stream_id, const std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(MakeRouteKey({}, stream_id));
    if (route == routes_.end() || route->second.generation != generation) return false;
    routes_.erase(route);
    return true;
}

bool FileTransferRouteRegistry::RemoveIfPluginMatches(
    const std::string& stream_id, const std::string& plugin_id) {
    return RemoveIfPluginMatches({}, stream_id, plugin_id);
}

bool FileTransferRouteRegistry::RemoveIfPluginMatches(
    const std::string& logical_session_id, const std::string& stream_id,
    const std::string& plugin_id) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(MakeRouteKey(logical_session_id, stream_id));
    if (route == routes_.end() || route->second.plugin_id != plugin_id) return false;
    routes_.erase(route);
    return true;
}

bool FileTransferRouteRegistry::RemoveIfConnectionMatches(
    const std::string& logical_session_id, const std::string& stream_id,
    const std::string& plugin_id, const std::string& connection_instance_id) {
    std::lock_guard lock(mutex_);
    const auto route = routes_.find(MakeRouteKey(logical_session_id, stream_id));
    if (route == routes_.end() || route->second.plugin_id != plugin_id ||
        route->second.connection_instance_id != connection_instance_id) return false;
    routes_.erase(route);
    return true;
}

bool FileTransferRouteRegistry::RemoveIfConnectionMatches(
    const std::string& stream_id, const std::string& plugin_id,
    const std::string& connection_instance_id) {
    return RemoveIfConnectionMatches({}, stream_id, plugin_id, connection_instance_id);
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
