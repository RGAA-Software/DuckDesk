#pragma once

#ifdef _WIN32
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "rdp_workspace_lease.h"

namespace px::rdp {

struct RdpProxyLaunch final {
    std::filesystem::path proxy_directory{};
    std::filesystem::path private_root{};
    std::string workspace_id{};
    std::string instance_id{};
    std::string node_id{};
    std::string device_id{};
    std::uint16_t proxy_port{};
    std::string target_certificate_sha256{};
    std::string proxy_certificate_sha256{};
    [[nodiscard]] std::string Entropy() const;
};

// Owns only its proxy subprocess, never the RDS process/session or user apps.
// The job is established while the subprocess is suspended. Partial failure and
// abrupt Render exit therefore cannot leave an unmanaged proxy behind.
class RdpProxyProcess final {
    struct ConstructionKey final {};

  public:
    [[nodiscard]] static std::unique_ptr<RdpProxyProcess> Start(const RdpProxyLaunch& launch, std::string& error);
    RdpProxyProcess(ConstructionKey, std::unique_ptr<RdpWorkspaceLease> lease, UniqueWinHandle job, UniqueWinHandle process,
                    std::filesystem::path configuration);
    ~RdpProxyProcess();
    RdpProxyProcess(const RdpProxyProcess&) = delete;
    RdpProxyProcess& operator=(const RdpProxyProcess&) = delete;
    void Stop() noexcept;
    [[nodiscard]] bool IsAlive() const noexcept;

  private:
    mutable std::mutex mutex_{};
    std::unique_ptr<RdpWorkspaceLease> lease_{};
    UniqueWinHandle job_{};
    UniqueWinHandle process_{};
    std::filesystem::path configuration_{};
};

} // namespace px::rdp
#endif
