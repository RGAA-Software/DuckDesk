#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace px::client::imgui {

enum class ClientFileLocationKind : std::uint8_t { Computer, Home, Desktop, Downloads, Documents, Pictures, Music, Videos, Drive };

struct ClientFileLocation final {
    ClientFileLocationKind kind{ClientFileLocationKind::Computer};
    std::string label{};
    std::string path{};
};

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
    [[nodiscard]] const std::vector<ClientFileLocation>& Locations() const noexcept;
    [[nodiscard]] const std::string& Error() const noexcept;
    bool Navigate(const std::string& path);
    bool NavigateUp();
    bool NavigateBack();
    bool NavigateHome();
    bool NavigateComputer();
    bool Refresh();
    bool CreateDirectory(const std::string& name);
    bool Remove(const std::string& path);
    bool Remove(const std::vector<std::string>& paths);
    bool Rename(const std::string& path, const std::string& newName);

  private:
    bool Load(const std::filesystem::path& path, bool addHistory);
    bool LoadComputer(bool addHistory);
    void RebuildLocations();
    [[nodiscard]] static bool ValidName(const std::string& name);

    std::filesystem::path path_{};
    std::vector<std::filesystem::path> history_{};
    std::vector<ClientLocalEntry> entries_{};
    std::vector<ClientFileLocation> locations_{};
    std::string displayPath_{};
    std::string error_{};
    bool computerView_{};
};

} // namespace px::client::imgui
