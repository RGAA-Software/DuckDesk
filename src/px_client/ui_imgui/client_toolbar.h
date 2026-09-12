#pragma once

#include <memory>
#include <string>

namespace px::client::imgui {

class ClientFileTransferPanel;
class ClientSession;

class ClientToolbar final {
  public:
    explicit ClientToolbar(std::shared_ptr<ClientFileTransferPanel> fileTransfer);
    void Draw(const std::shared_ptr<ClientSession>& session, bool english);
    [[nodiscard]] bool Visible() const noexcept;

  private:
    std::shared_ptr<ClientFileTransferPanel> fileTransfer_{};
    bool visible_{true};
    bool showStatistics_{};
    bool audioEnabled_{true};
    bool microphoneMuted_{};
    bool speakerMuted_{};
    std::string screenshotStatus_{};
    int frameRate_{60};
    int resolutionWidth_{};
    int resolutionHeight_{};
};

} // namespace px::client::imgui
