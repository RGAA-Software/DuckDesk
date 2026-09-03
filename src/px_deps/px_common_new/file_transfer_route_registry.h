#ifndef PX_COMMON_NEW_FILE_TRANSFER_ROUTE_REGISTRY_H
#define PX_COMMON_NEW_FILE_TRANSFER_ROUTE_REGISTRY_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace px {

struct FileTransferRoute {
    std::string logical_session_id;
    std::string plugin_id;
    std::string connection_instance_id;
    std::uint64_t generation = 0;

    bool operator==(const FileTransferRoute&) const = default;
};

class FileTransferRouteRegistry final {
public:
    [[nodiscard]] FileTransferRoute Bind(const std::string& logical_session_id,
                                         const std::string& stream_id,
                                         const std::string& plugin_id,
                                         const std::string& connection_instance_id);
    [[nodiscard]] FileTransferRoute Bind(const std::string& stream_id,
                                         const std::string& plugin_id,
                                         const std::string& connection_instance_id = {});
    [[nodiscard]] std::optional<FileTransferRoute> Resolve(
        const std::string& stream_id) const;
    [[nodiscard]] std::optional<FileTransferRoute> Resolve(
        const std::string& logical_session_id, const std::string& stream_id) const;
    // Legacy FT engines currently retain only the Console-issued stream id.
    // Resolve succeeds only when that id maps to exactly one logical route;
    // ambiguity is rejected instead of selecting another session's channel.
    [[nodiscard]] std::optional<FileTransferRoute> ResolveUniqueStream(
        const std::string& stream_id) const;
    bool Remove(const std::string& stream_id);
    bool Remove(const std::string& logical_session_id, const std::string& stream_id);
    bool RemoveIfGenerationMatches(const std::string& stream_id,
                                   std::uint64_t generation);
    bool RemoveIfPluginMatches(const std::string& stream_id,
                               const std::string& plugin_id);
    bool RemoveIfPluginMatches(const std::string& logical_session_id,
                               const std::string& stream_id,
                               const std::string& plugin_id);
    bool RemoveIfConnectionMatches(const std::string& stream_id,
                                   const std::string& plugin_id,
                                   const std::string& connection_instance_id);
    bool RemoveIfConnectionMatches(const std::string& logical_session_id,
                                   const std::string& stream_id,
                                   const std::string& plugin_id,
                                   const std::string& connection_instance_id);
    void Clear();
    [[nodiscard]] std::size_t Size() const;

private:
    [[nodiscard]] static std::string MakeRouteKey(const std::string& logical_session_id,
                                                   const std::string& stream_id);
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FileTransferRoute> routes_;
    std::uint64_t next_generation_ = 1;
};

} // namespace px

#endif // PX_COMMON_NEW_FILE_TRANSFER_ROUTE_REGISTRY_H
