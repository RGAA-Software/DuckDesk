#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace px::panel::ui {

enum class AccountOperationState : std::uint8_t { Idle, Working, Succeeded, Failed };

struct AccountSnapshot final {
    bool loggedIn{false};
    std::string username{};
    std::string avatarPath{};
    AccountOperationState operation{AccountOperationState::Idle};
};

class AccountPort {
  public:
    virtual ~AccountPort() = default;
    virtual AccountSnapshot Snapshot() const = 0;
    virtual void Login(std::string username, std::string password) = 0;
    virtual void Register(std::string username, std::string password) = 0;
    virtual void UpdateProfile(std::string username) = 0;
    virtual void UpdatePassword(std::string currentPassword, std::string newPassword) = 0;
    virtual void UpdateAvatar(std::string imagePath) = 0;
    virtual void Logout() = 0;
};

std::shared_ptr<AccountPort> CreatePreviewAccountPort();

} // namespace px::panel::ui
