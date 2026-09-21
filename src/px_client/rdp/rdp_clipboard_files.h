#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace px::rdp {

inline constexpr std::size_t kMaximumClipboardFileCount{512U};
inline constexpr std::uint64_t kMaximumClipboardFileBytes{512ULL * 1024ULL * 1024ULL};
inline constexpr std::size_t kClipboardFileChunkBytes{64U * 1024U};

class ClipboardStagingDirectory final {
public:
    class CreationKey final {
        friend class ClipboardStagingDirectory;

    private:
        CreationKey() = default;
    };

    static std::shared_ptr<ClipboardStagingDirectory> Create();
    ClipboardStagingDirectory(CreationKey, std::filesystem::path root);
    ~ClipboardStagingDirectory();

    ClipboardStagingDirectory(const ClipboardStagingDirectory&) = delete;
    ClipboardStagingDirectory& operator=(const ClipboardStagingDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& Root() const noexcept;

private:
    std::filesystem::path root_{};
};

struct LocalClipboardFile final {
    std::filesystem::path source{};
    std::filesystem::path relative{};
    std::uint64_t size{};
    FILETIME lastWrite{};
    bool directory{};
};

struct RemoteClipboardFile final {
    std::filesystem::path relative{};
    std::uint64_t advertisedSize{};
    FILETIME lastWrite{};
    bool directory{};
};

[[nodiscard]] std::vector<LocalClipboardFile> InventoryLocalClipboardFiles(std::span<const std::filesystem::path> roots);
[[nodiscard]] std::vector<std::uint8_t> SerializeLocalClipboardFiles(std::span<const LocalClipboardFile> files);
[[nodiscard]] std::vector<RemoteClipboardFile> ParseRemoteClipboardFiles(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::filesystem::path> ClipboardTopLevelPaths(const std::filesystem::path& stagingRoot,
                                                                        std::span<const RemoteClipboardFile> files);
[[nodiscard]] bool ReadClipboardFileRange(const LocalClipboardFile& file, std::uint64_t offset, std::size_t requestedBytes,
                                          std::vector<std::uint8_t>& output);

}  // namespace px::rdp
