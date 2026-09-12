#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace px::panel::ui {

struct CloudApplicationCard final {
    std::string streamId{};
    std::string name{};
    std::string instanceState{};
    bool rdpMode{false};
    bool forceTcp{false};
    bool forceRelay{false};
};

struct CloudApplicationPasswordRequest final {
    std::string streamId{};
    std::string applicationName{};
};

class CloudApplicationsPort {
  public:
    virtual ~CloudApplicationsPort() = default;
    virtual std::vector<CloudApplicationCard> Snapshot() = 0;
    virtual void Refresh() = 0;
    virtual void Start(const std::string& streamId, bool viewOnly) = 0;
    [[nodiscard]] virtual std::optional<CloudApplicationPasswordRequest> PendingPasswordRequest() const = 0;
    virtual void SubmitPassword(const std::string& streamId, std::string password) = 0;
    virtual void CancelPassword(const std::string& streamId) = 0;
    virtual void Stop(const std::string& streamId) = 0;
    virtual void SetForceTcp(const std::string& streamId, bool enabled) = 0;
    virtual void SetForceRelay(const std::string& streamId, bool enabled) = 0;
};

std::shared_ptr<CloudApplicationsPort> CreatePreviewCloudApplicationsPort();

} // namespace px::panel::ui
