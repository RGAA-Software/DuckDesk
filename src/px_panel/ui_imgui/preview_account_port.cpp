#include "account_port.h"

#include <utility>

namespace px::panel::ui {
namespace {

class PreviewAccountPort final : public AccountPort {
  public:
    AccountSnapshot Snapshot() const override { return snapshot_; }
    void Login(std::string username, std::string) override {
        snapshot_ = {.loggedIn = true, .username = std::move(username), .operation = AccountOperationState::Succeeded};
    }
    void Register(std::string username, std::string password) override { Login(std::move(username), std::move(password)); }
    void Logout() override { snapshot_ = {}; }

  private:
    AccountSnapshot snapshot_{};
};

} // namespace

std::shared_ptr<AccountPort> CreatePreviewAccountPort() { return std::make_shared<PreviewAccountPort>(); }

} // namespace px::panel::ui
