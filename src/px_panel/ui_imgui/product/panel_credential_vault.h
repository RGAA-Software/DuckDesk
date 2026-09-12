#pragma once

#include <memory>
#include <optional>
#include <string>

namespace px::panel::product {

class PanelCredentialVault final {
  public:
    static std::shared_ptr<PanelCredentialVault> Create();

    [[nodiscard]] std::optional<std::string> Read(const std::string& target) const;
    [[nodiscard]] bool Write(const std::string& target, const std::string& password) const;
    void Delete(const std::string& target) const;

  private:
    [[nodiscard]] static std::wstring CredentialTarget(const std::string& target);
};

} // namespace px::panel::product
