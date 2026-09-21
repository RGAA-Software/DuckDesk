#include "rdp_clipboard_files.h"

#include <freerdp/channels/channels.h>
#include <freerdp/utils/cliprdr_utils.h>
#include <winpr/shell.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

namespace px::rdp {
namespace {

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) {
        if (component.empty() || component == L"." || component == L".." || component.native().find(L':') != std::wstring::npos) return false;
    }
    return path.native().size() < MAX_PATH;
}

std::filesystem::path NormalizeDescriptorPath(const wchar_t (&name)[MAX_PATH]) {
    const auto end = std::find(std::begin(name), std::end(name), L'\0');
    if (end == std::begin(name) || end == std::end(name)) return {};
    std::wstring value{std::begin(name), end};
    std::ranges::replace(value, L'/', L'\\');
    const std::filesystem::path path{value};
    return IsSafeRelativePath(path) ? path.lexically_normal() : std::filesystem::path{};
}

bool ReadMetadata(const std::filesystem::path& source, LocalClipboardFile& file) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &attributes)) return false;
    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
    file.directory = (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    file.size = file.directory ? 0 : (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32U) | attributes.nFileSizeLow;
    file.lastWrite = attributes.ftLastWriteTime;
    return file.directory || (attributes.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0;
}

bool AppendEntry(const std::filesystem::path& source, const std::filesystem::path& relative, std::vector<LocalClipboardFile>& files,
                 std::uint64_t& totalBytes) {
    if (!IsSafeRelativePath(relative) || files.size() >= kMaximumClipboardFileCount) return false;
    LocalClipboardFile file{.source = source, .relative = relative};
    if (!ReadMetadata(source, file) || file.size > kMaximumClipboardFileBytes - totalBytes) return false;
    totalBytes += file.size;
    files.push_back(std::move(file));
    return true;
}

std::uint64_t DescriptorSize(const FILEDESCRIPTORW& descriptor) {
    return (static_cast<std::uint64_t>(descriptor.nFileSizeHigh) << 32U) | descriptor.nFileSizeLow;
}

}  // namespace

ClipboardStagingDirectory::ClipboardStagingDirectory(CreationKey, std::filesystem::path root) : root_{std::move(root)} {}

std::shared_ptr<ClipboardStagingDirectory> ClipboardStagingDirectory::Create() {
    std::array<wchar_t, MAX_PATH + 1U> temporaryRoot{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(temporaryRoot.size()), temporaryRoot.data());
    if (length == 0 || length >= temporaryRoot.size()) return {};
    GUID identifier{};
    if (CoCreateGuid(&identifier) != S_OK) return {};
    std::array<wchar_t, 40> identifierText{};
    if (StringFromGUID2(identifier, identifierText.data(), static_cast<int>(identifierText.size())) <= 0) return {};
    const auto root = std::filesystem::path{temporaryRoot.data()} / L"Pixels" / L"RdpClipboard" / identifierText.data();
    std::error_code error{};
    if (!std::filesystem::create_directories(root, error) || error) return {};
    return std::make_shared<ClipboardStagingDirectory>(CreationKey{}, root);
}

ClipboardStagingDirectory::~ClipboardStagingDirectory() {
    std::error_code error{};
    std::filesystem::remove_all(root_, error);
}

const std::filesystem::path& ClipboardStagingDirectory::Root() const noexcept { return root_; }

std::vector<LocalClipboardFile> InventoryLocalClipboardFiles(const std::span<const std::filesystem::path> roots) {
    if (roots.empty() || roots.size() > kMaximumClipboardFileCount) return {};
    std::vector<LocalClipboardFile> files{};
    std::set<std::wstring, std::less<>> topLevelNames{};
    std::uint64_t totalBytes{};
    for (const auto& root : roots) {
        std::error_code error{};
        const auto absolute = std::filesystem::absolute(root, error).lexically_normal();
        if (error || absolute.filename().empty() || !topLevelNames.insert(absolute.filename().native()).second ||
            !AppendEntry(absolute, absolute.filename(), files, totalBytes)) {
            return {};
        }
        if (!files.back().directory) continue;
        std::filesystem::recursive_directory_iterator iterator{absolute, std::filesystem::directory_options::skip_permission_denied, error};
        const std::filesystem::recursive_directory_iterator end{};
        for (; !error && iterator != end; iterator.increment(error)) {
            if (iterator->is_symlink(error) || error) return {};
            const auto relative = absolute.filename() / std::filesystem::relative(iterator->path(), absolute, error);
            if (error || !AppendEntry(iterator->path(), relative, files, totalBytes)) return {};
        }
        if (error) return {};
    }
    return files;
}

std::vector<std::uint8_t> SerializeLocalClipboardFiles(const std::span<const LocalClipboardFile> files) {
    if (files.empty() || files.size() > kMaximumClipboardFileCount) return {};
    std::vector<FILEDESCRIPTORW> descriptors(files.size());
    for (std::size_t index{}; index < files.size(); ++index) {
        const auto& file = files[index];
        auto& descriptor = descriptors[index];
        descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_WRITESTIME;
        descriptor.dwFileAttributes = file.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        descriptor.ftLastWriteTime = file.lastWrite;
        descriptor.nFileSizeHigh = static_cast<DWORD>(file.size >> 32U);
        descriptor.nFileSizeLow = static_cast<DWORD>(file.size);
        const auto& name = file.relative.native();
        if (name.empty() || name.size() >= std::size(descriptor.cFileName)) return {};
        std::copy(name.begin(), name.end(), descriptor.cFileName);
    }
    BYTE* serialized{};  // NOLINT(pixels-raw-pointer-boundary): FreeRDP allocation output boundary.
    UINT32 serializedBytes{};
    if (cliprdr_serialize_file_list(descriptors.data(), static_cast<UINT32>(descriptors.size()), &serialized, &serializedBytes) != CHANNEL_RC_OK ||
        !serialized || serializedBytes == 0) {
        std::free(serialized);
        return {};
    }
    std::vector<std::uint8_t> result(serialized, serialized + serializedBytes);
    std::free(serialized);
    return result;
}

std::vector<RemoteClipboardFile> ParseRemoteClipboardFiles(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(UINT32)) return {};
    const auto count = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                       (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (count == 0 || count > kMaximumClipboardFileCount || bytes.size() != 4U + static_cast<std::size_t>(count) * 592U) return {};
    FILEDESCRIPTORW* parsed{};  // NOLINT(pixels-raw-pointer-boundary): FreeRDP allocation output boundary.
    UINT32 parsedCount{};
    if (cliprdr_parse_file_list(bytes.data(), static_cast<UINT32>(bytes.size()), &parsed, &parsedCount) != CHANNEL_RC_OK || !parsed ||
        parsedCount != count) {
        std::free(parsed);
        return {};
    }
    std::vector<RemoteClipboardFile> files{};
    files.reserve(parsedCount);
    std::uint64_t totalBytes{};
    for (UINT32 index{}; index < parsedCount; ++index) {
        const auto relative = NormalizeDescriptorPath(parsed[index].cFileName);
        const bool directory = (parsed[index].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const auto size = directory ? 0 : DescriptorSize(parsed[index]);
        if (relative.empty() || size > kMaximumClipboardFileBytes - totalBytes) {
            files.clear();
            break;
        }
        totalBytes += size;
        files.push_back({.relative = relative, .advertisedSize = size, .lastWrite = parsed[index].ftLastWriteTime, .directory = directory});
    }
    std::free(parsed);
    return files;
}

std::vector<std::filesystem::path> ClipboardTopLevelPaths(const std::filesystem::path& stagingRoot,
                                                          const std::span<const RemoteClipboardFile> files) {
    std::set<std::filesystem::path> unique{};
    for (const auto& file : files) {
        const auto first = *file.relative.begin();
        unique.insert(stagingRoot / first);
    }
    return {unique.begin(), unique.end()};
}

bool ReadClipboardFileRange(const LocalClipboardFile& file, const std::uint64_t offset, const std::size_t requestedBytes,
                            std::vector<std::uint8_t>& output) {
    output.clear();
    if (file.directory || offset > file.size || requestedBytes > kClipboardFileChunkBytes) return false;
    const auto available = static_cast<std::size_t>(std::min<std::uint64_t>(requestedBytes, file.size - offset));
    output.resize(available);
    if (available == 0) return true;
    std::ifstream stream{file.source, std::ios::binary};
    if (!stream || offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char*>(output.data()),
                static_cast<std::streamsize>(available));  // NOLINT(pixels-raw-pointer-boundary): sync stream ABI.
    return stream.good() || (stream.eof() && static_cast<std::size_t>(stream.gcount()) == available);
}

}  // namespace px::rdp
