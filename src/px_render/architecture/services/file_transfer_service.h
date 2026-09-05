#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "file_transfer_types.h"
#include "modules/builtin_module_catalog.h"
#include "px_common/file_transfer_route_registry.h"
#include "px_common/file_transfer_send_result.h"

namespace px::ft {
class FtAsyncSession;
class FtEngine;
}

namespace px {

class Data;
class FileAction;
class Message;
class PxAsyncRuntime;

namespace render {

inline constexpr std::string_view kFileTransferModuleId =
    "3f6c2a18-7e4b-4d59-9a21-8c0e5b6d7f2a";

struct FileTransferAuditBegin final {
    std::string file_id;
    std::int64_t begin_timestamp{0};
    std::string visitor_device_id;
    std::string direction;
    std::string file_detail;
};

struct FileTransferAuditEnd final {
    std::string file_id;
    std::int64_t end_timestamp{0};
    std::int64_t duration{0};
    bool success{false};
    std::string status;
    std::string reason;
};

struct FileTransferServiceOptions final {
    std::string device_id;
    bool enabled{true};
    std::uint64_t max_transmit_speed_bits_per_second{0};
};

struct FileTransferServiceSnapshot final {
    bool running{false};
    bool enabled{true};
    std::size_t sessions{0};
    std::size_t audits{0};
    std::uint64_t accepted_messages{0};
    std::uint64_t rejected_messages{0};
};

class FileTransferService final
    : public std::enable_shared_from_this<FileTransferService> {
public:
    using SendCallback = std::function<FileTransferSendResult(
        const std::string& transport_id,
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        const std::string& connection_id)>;
    using AuditBeginCallback =
        std::function<void(const FileTransferAuditBegin&)>;
    using AuditEndCallback =
        std::function<void(const FileTransferAuditEnd&)>;

    [[nodiscard]] static std::shared_ptr<FileTransferService> Create(
        FileTransferServiceOptions options,
        SendCallback send_callback,
        AuditBeginCallback audit_begin_callback,
        AuditEndCallback audit_end_callback);

    FileTransferService(
        FileTransferServiceOptions options,
        SendCallback send_callback,
        AuditBeginCallback audit_begin_callback,
        AuditEndCallback audit_end_callback);
    ~FileTransferService();

    FileTransferService(const FileTransferService&) = delete;
    FileTransferService& operator=(const FileTransferService&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void HandleInbound(const FileTransferInbound& inbound);
    void HandleRouteDisconnected(
        const FileTransferRouteDisconnected& disconnected);
    void HandleClientDisconnected(
        const std::string& visitor_device_id,
        const std::string& stream_id);
    void UpdateRateLimit(std::uint64_t max_transmit_speed_bits_per_second);
    [[nodiscard]] FileTransferServiceSnapshot Snapshot() const;

private:
    class AsyncBridge;

    void StopSessions();
    void RetireSession(const std::string& logical_session_id,
                       const std::string& stream_id, bool close_audits);
    std::shared_ptr<px::ft::FtAsyncSession> GetOrCreateSession(
        const std::string& logical_session_id, const std::string& stream_id);
    void ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                        const std::shared_ptr<Message>& message,
                        const std::string& logical_session_id,
                        const std::string& transport_id,
                        const std::string& connection_id);
    void ProcessRouteDisconnected(const std::string& logical_session_id,
                                  const std::string& stream_id,
                                  const std::string& transport_id,
                                  const std::string& connection_id);
    void HandleOverwriteFallback(const std::string& logical_session_id,
                                 const std::string& stream_id,
                                 std::int32_t job_id,
                                 std::int32_t file_num);
    FileTransferSendResult SendToChannel(
        const std::string& logical_session_id,
        const Message& message,
        const std::string& stream_id);
    void ReplyNoPermission(const std::shared_ptr<Message>& message,
                           const std::string& transport_id,
                           const std::string& connection_id);
    bool CheckFileCountLimit(const FileAction& action,
                             const std::string& logical_session_id,
                             const std::string& stream_id);
    bool CheckReadPathExists(const FileAction& action,
                             const std::string& logical_session_id,
                             const std::string& stream_id);
    void TrackJobBegin(const std::string& logical_session_id,
                       const std::string& stream_id,
                       std::int32_t job_id,
                       const std::string& direction,
                       const std::string& path,
                       std::uint64_t total_size,
                       const std::shared_ptr<Message>& message);
    void TrackJobEnd(const std::string& logical_session_id,
                     const std::string& stream_id,
                     std::int32_t job_id,
                     const std::string& error);
    void CloseAudits(const std::optional<std::string>& logical_session_id,
                     const std::string& stream_id,
                     bool success);
    [[nodiscard]] static std::string MakeSessionKey(
        const std::string& logical_session_id, const std::string& stream_id);

    FileTransferServiceOptions options_;
    SendCallback send_callback_;
    AuditBeginCallback audit_begin_callback_;
    AuditEndCallback audit_end_callback_;
    std::shared_ptr<PxAsyncRuntime> async_runtime_;
    std::shared_ptr<AsyncBridge> async_bridge_;
    mutable std::mutex route_session_mutex_;
    mutable std::mutex sessions_mutex_;
    mutable std::mutex audits_mutex_;
    std::unordered_map<std::string, std::shared_ptr<px::ft::FtAsyncSession>> sessions_;
    FileTransferRouteRegistry routes_;
    std::atomic_bool accepting_{false};
    std::atomic_bool enabled_{true};
    std::atomic_uint64_t accepted_messages_{0};
    std::atomic_uint64_t rejected_messages_{0};

    struct AuditRecord final {
        std::string file_id;
        std::int64_t begin_timestamp{0};
        std::string logical_session_id;
        std::string stream_id;
    };
    std::unordered_map<std::string, AuditRecord> audits_;
};

}  // namespace render
}  // namespace px
