#pragma once

#include <memory>
#include <string>

namespace px::client::imgui {

class ClientSession;

class ClientFileTransferPanel final {
  public:
    void Draw(const std::shared_ptr<ClientSession>& session, bool english);
    void Open();
    [[nodiscard]] bool CapturesPointer(float x, float y) const noexcept;
    [[nodiscard]] bool CapturesKeyboard() const noexcept;

  private:
    bool open_{};
    std::string localPath_{};
    std::string remotePath_{};
    std::string selectedRemote_{};
    bool applyOverwriteToAll_{};
    float windowX_{};
    float windowY_{};
    float windowWidth_{};
    float windowHeight_{};
    bool capturesKeyboard_{};
};

} // namespace px::client::imgui
