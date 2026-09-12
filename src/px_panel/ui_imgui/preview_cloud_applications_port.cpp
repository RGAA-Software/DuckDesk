#include "cloud_applications_port.h"

namespace px::panel::ui {
namespace {

class PreviewCloudApplicationsPort final : public CloudApplicationsPort {
  public:
    std::vector<CloudApplicationCard> Snapshot() override {
        return {{.streamId = "preview-app", .name = "2dAdventure", .instanceState = "running"}};
    }
    void Refresh() override {}
    void Start(const std::string&, bool) override {}
    std::optional<CloudApplicationPasswordRequest> PendingPasswordRequest() const override { return std::nullopt; }
    void SubmitPassword(const std::string&, std::string) override {}
    void CancelPassword(const std::string&) override {}
    void Stop(const std::string&) override {}
    void SetForceTcp(const std::string&, bool) override {}
    void SetForceRelay(const std::string&, bool) override {}
};

} // namespace

std::shared_ptr<CloudApplicationsPort> CreatePreviewCloudApplicationsPort() {
    return std::make_shared<PreviewCloudApplicationsPort>();
}

} // namespace px::panel::ui
