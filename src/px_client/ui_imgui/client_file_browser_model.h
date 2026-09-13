#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace px::client::imgui {

enum class ClientFileSortColumn : std::uint8_t { Name, Modified, Size };

struct ClientFileListItem final {
    std::string path{};
    std::string name{};
    std::uint64_t size{};
    std::uint64_t modifiedTime{};
    bool directory{};
};

class ClientFileSelection final {
  public:
    void Select(std::size_t index, std::string path, bool control, bool shift, const std::vector<ClientFileListItem>& visibleItems);
    void SelectOnly(std::size_t index, std::string path);
    void SelectAll(const std::vector<ClientFileListItem>& visibleItems);
    void Clear() noexcept;
    [[nodiscard]] bool Contains(std::string_view path) const;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Paths() const noexcept;

  private:
    std::vector<std::string> paths_{};
    std::optional<std::size_t> anchor_{};
};

struct ClientFileSort final {
    ClientFileSortColumn column{ClientFileSortColumn::Name};
    bool ascending{true};

    void Toggle(ClientFileSortColumn value) noexcept;
    void Apply(std::vector<ClientFileListItem>& items) const;
};

} // namespace px::client::imgui
