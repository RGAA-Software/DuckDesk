#include "panel_product_runtime.h"

#include <utility>

namespace px::panel::product {
namespace {

class ProductAccountPort final : public ui::AccountPort, public std::enable_shared_from_this<ProductAccountPort> {
  public:
    explicit ProductAccountPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {}

    ui::AccountSnapshot Snapshot() const override {
        return runtime_->Console()->Account();
    }
    void Login(std::string username, std::string password) override {
        Start(std::move(username), std::move(password), false);
    }
    void Register(std::string username, std::string password) override {
        Start(std::move(username), std::move(password), true);
    }
    void UpdateProfile(std::string username) override {
        RunAccountOperation([username = std::move(username)](const std::shared_ptr<PanelProductRuntime>& runtime) {
            return runtime->Console()->UpdateProfile(username);
        }, "Profile updated", "Profile update failed");
    }
    void UpdatePassword(std::string currentPassword, std::string newPassword) override {
        RunAccountOperation(
            [currentPassword = std::move(currentPassword), newPassword = std::move(newPassword)](
                const std::shared_ptr<PanelProductRuntime>& runtime) { return runtime->Console()->UpdatePassword(currentPassword, newPassword); },
            "Password updated", "Password update failed");
    }
    void UpdateAvatar(std::string imagePath) override {
        RunAccountOperation([imagePath = std::move(imagePath)](const std::shared_ptr<PanelProductRuntime>& runtime) {
            return runtime->Console()->UpdateAvatar(imagePath);
        }, "Avatar updated", "Avatar update failed");
    }
    void Logout() override {
        if (runtime_->Console()->Account().operation == ui::AccountOperationState::Working)
            return;
        runtime_->Console()->SetAccountOperation(ui::AccountOperationState::Working);
        const auto runtime = runtime_;
        const std::weak_ptr<ProductAccountPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf] {
            const bool success = runtime->Console()->Logout();
            if (!weakSelf.lock())
                return;
            runtime->Console()->SetAccountOperation(success ? ui::AccountOperationState::Succeeded : ui::AccountOperationState::Failed);
            runtime->Notify(!success, success ? "Pixels" : "Error", success ? "Signed out" : "Sign out failed");
        }));
    }

  private:
    template <typename Operation>
    void RunAccountOperation(Operation operation, std::string successMessage, std::string failureMessage) {
        if (runtime_->Console()->Account().operation == ui::AccountOperationState::Working)
            return;
        runtime_->Console()->SetAccountOperation(ui::AccountOperationState::Working);
        const auto runtime = runtime_;
        const std::weak_ptr<ProductAccountPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post(
            [runtime, weakSelf, operation = std::move(operation), successMessage = std::move(successMessage),
             failureMessage = std::move(failureMessage)] {
                const bool success = operation(runtime);
                if (!weakSelf.lock())
                    return;
                runtime->Console()->SetAccountOperation(success ? ui::AccountOperationState::Succeeded : ui::AccountOperationState::Failed);
                runtime->Notify(!success, success ? "Pixels" : "Error", success ? successMessage : failureMessage);
            }));
    }

    void Start(std::string username, std::string password, const bool registration) {
        if (runtime_->Console()->Account().operation == ui::AccountOperationState::Working)
            return;
        runtime_->Console()->SetAccountOperation(ui::AccountOperationState::Working);
        const auto runtime = runtime_;
        const std::weak_ptr<ProductAccountPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, username = std::move(username), password = std::move(password), registration] {
            const bool success = registration ? runtime->Console()->Register(username, password) : runtime->Console()->Login(username, password);
            if (!weakSelf.lock())
                return;
            runtime->Console()->SetAccountOperation(success ? ui::AccountOperationState::Succeeded : ui::AccountOperationState::Failed);
            runtime->Notify(!success, success ? "Pixels" : "Error",
                            success ? (registration ? "Account created" : "Signed in") : (registration ? "Registration failed" : "Sign in failed"));
        }));
    }

    std::shared_ptr<PanelProductRuntime> runtime_{};
};

} // namespace

std::shared_ptr<ui::AccountPort> CreateProductAccountPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductAccountPort>(runtime);
}

} // namespace px::panel::product
