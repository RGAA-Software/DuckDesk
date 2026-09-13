#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace px::client::imgui {

struct ClientLocalEntry final {
    std::string name{};
    std::string path{};
    std::uint64_t size{};
    std::uint64_t modifiedTime{};
    bool directory{};
    bool hidden{};
};

class ClientLocalFileSystem final {
  public:
    ClientLocalFileSystem();

    [[nodiscard]] const std::string& Path() const noexcept;
    [[nodiscard]] const std::vector<ClientLocalEntry>& Entries() const noexcept;
    [[nodiscard]] const std::string& Error() const noexcept;
    bool Navigate(const std::string& path);
    bool NavigateUp();
    bool NavigateBack();
    bool NavigateHome();
    bool Refresh();
    bool CreateDirectory(const std::string& name);
    bool Remove(const std::string& path);
    bool Remove(const std::vector<std::string>& paths);
    bool Rename(const std::string& path, const std::string& newName);

  private:
    bool Load(const std::filesystem::path& path, bool addHistory);
    [[nodiscard]] static bool ValidName(const std::string& name);

    std::filesystem::path path_{};
    std::vector<std::filesystem::path> history_{};
    std::vector<ClientLocalEntry> entries_{};
    std::string displayPath_{};
    std::string error_{};
};

} // namespace px::client::imgui
