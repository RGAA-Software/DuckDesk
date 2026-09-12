#pragma once

#include <array>
#include <memory>
#include <string>

namespace px::client::imgui {

class ClientSession;

class ClientFileTransferPanel final {
  public:
    void Draw(const std::shared_ptr<ClientSession>& session, bool english);
    void Open();

  private:
    bool open_{};
    std::array<char, 4096> localPath_{};
    std::array<char, 4096> remotePath_{};
    std::string selectedRemote_{};
    bool applyOverwriteToAll_{};
};

} // namespace px::client::imgui
