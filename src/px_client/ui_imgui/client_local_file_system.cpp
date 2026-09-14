#include "client_local_file_system.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#undef CreateDirectory
#endif

namespace px::client::imgui {
namespace {

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::uint64_t ModifiedSeconds(const std::filesystem::directory_entry& entry, std::error_code& error) {
    const auto value = entry.last_write_time(error);
    if (error)
        return 0;
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(value - std::filesystem::file_time_type::clock::now() +
                                                                                              std::chrono::system_clock::now());
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
    return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
}

bool CaseInsensitiveLess(const std::string& left, const std::string& right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                        [](const unsigned char lhs, const unsigned char rhs) { return std::tolower(lhs) < std::tolower(rhs); });
}

bool IsHidden(const std::filesystem::path& path) {
#ifdef _WIN32
    const DWORD attributes{GetFileAttributesW(path.c_str())};
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0U;
#else
    const auto name = path.filename().string();
    return !name.empty() && name.front() == '.';
#endif
}

} // namespace

ClientLocalFileSystem::ClientLocalFileSystem() {
    RebuildLocations();
    static_cast<void>(LoadComputer(false));
}

const std::string& ClientLocalFileSystem::Path() const noexcept {
    return displayPath_;
}

const std::vector<ClientLocalEntry>& ClientLocalFileSystem::Entries() const noexcept {
    return entries_;
}

const std::vector<ClientFileLocation>& ClientLocalFileSystem::Locations() const noexcept {
    return locations_;
}

const std::string& ClientLocalFileSystem::Error() const noexcept {
    return error_;
}

bool ClientLocalFileSystem::Navigate(const std::string& path) {
    if (path.empty() || path.size() > 4096U)
        return false;
#ifdef _WIN32
    if (path == "/")
        return LoadComputer(true);
#endif
    return Load(std::filesystem::u8path(path), true);
}

bool ClientLocalFileSystem::NavigateUp() {
    const auto parent = path_.parent_path();
    return parent.empty() || parent == path_ ? LoadComputer(true) : Load(parent, true);
}

bool ClientLocalFileSystem::NavigateBack() {
    if (history_.empty())
        return false;
    const auto previous = history_.back();
    history_.pop_back();
    return Load(previous, false);
}

bool ClientLocalFileSystem::NavigateHome() {
    const auto home = std::ranges::find(locations_, ClientFileLocationKind::Home, &ClientFileLocation::kind);
    if (home != locations_.end())
        return Load(std::filesystem::u8path(home->path), true);
#ifdef _WIN32
    std::array<wchar_t, 32'768> value{};
    const DWORD size{GetEnvironmentVariableW(L"USERPROFILE", value.data(), static_cast<DWORD>(value.size()))};
    return size > 0U && size < value.size() && Load(std::filesystem::path{value.data()}, true);
#else
    return Load(std::filesystem::path{std::getenv("HOME")}, true); // NOLINT(gammaray-raw-pointer-boundary): CRT boundary
#endif
}

bool ClientLocalFileSystem::NavigateComputer() {
    return LoadComputer(true);
}

bool ClientLocalFileSystem::Refresh() {
    return computerView_ ? LoadComputer(false) : Load(path_, false);
}

bool ClientLocalFileSystem::CreateDirectory(const std::string& name) {
    if (computerView_) {
        error_ = "Select a drive or folder before creating a directory.";
        return false;
    }
    if (!ValidName(name)) {
        error_ = "The folder name is invalid.";
        return false;
    }
    std::error_code error{};
    if (!std::filesystem::create_directory(path_ / std::filesystem::u8path(name), error) || error) {
        error_ = error ? error.message() : "The folder already exists.";
        return false;
    }
    return Refresh();
}

bool ClientLocalFileSystem::Remove(const std::string& path) {
    return Remove(std::vector<std::string>{path});
}

bool ClientLocalFileSystem::Remove(const std::vector<std::string>& paths) {
    if (computerView_ || paths.empty())
        return false;
    for (const auto& path : paths) {
        const auto target = std::filesystem::u8path(path);
        if (target.empty() || target.parent_path() != path_) {
            error_ = "The selected item is outside the current directory.";
            return false;
        }
        std::error_code error{};
        static_cast<void>(std::filesystem::remove_all(target, error));
        if (error) {
            error_ = error.message();
            return false;
        }
    }
    return Refresh();
}

bool ClientLocalFileSystem::Rename(const std::string& path, const std::string& newName) {
    if (computerView_)
        return false;
    const auto source = std::filesystem::u8path(path);
    if (!ValidName(newName) || source.empty() || source.parent_path() != path_) {
        error_ = "The new name is invalid.";
        return false;
    }
    std::error_code error{};
    std::filesystem::rename(source, path_ / std::filesystem::u8path(newName), error);
    if (error) {
        error_ = error.message();
        return false;
    }
    return Refresh();
}

bool ClientLocalFileSystem::ValidName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

bool ClientLocalFileSystem::Load(const std::filesystem::path& path, const bool addHistory) {
#ifdef _WIN32
    if (path.generic_string() == "/")
        return LoadComputer(addHistory);
#endif
    std::error_code error{};
    if (!std::filesystem::is_directory(path, error) || error) {
        error_ = error ? error.message() : "The selected path is not a directory.";
        return false;
    }

    std::vector<ClientLocalEntry> entries{};
    std::filesystem::directory_iterator iterator{path, std::filesystem::directory_options::skip_permission_denied, error};
    const std::filesystem::directory_iterator end{};
    while (!error && iterator != end) {
        const auto& item = *iterator;
        std::error_code metadataError{};
        const bool directory = item.is_directory(metadataError);
        if (!metadataError) {
            const std::uint64_t size{directory ? 0U : item.file_size(metadataError)};
            if (metadataError)
                metadataError.clear();
            entries.push_back({.name = Utf8(item.path().filename()),
                               .path = Utf8(item.path()),
                               .size = size,
                               .modifiedTime = ModifiedSeconds(item, metadataError),
                               .directory = directory,
                               .hidden = IsHidden(item.path())});
        }
        iterator.increment(error);
    }
    if (error) {
        error_ = error.message();
        return false;
    }
    std::ranges::sort(entries, [](const ClientLocalEntry& left, const ClientLocalEntry& right) {
        if (left.directory != right.directory)
            return left.directory > right.directory;
        return CaseInsensitiveLess(left.name, right.name);
    });
    if (addHistory && !path_.empty() && path_ != path)
        history_.push_back(path_);
    path_ = path;
    computerView_ = false;
    displayPath_ = Utf8(path_);
    entries_ = std::move(entries);
    error_.clear();
    return true;
}

void ClientLocalFileSystem::RebuildLocations() {
    locations_.clear();
    locations_.push_back({.kind = ClientFileLocationKind::Computer, .path = "/"});
#ifdef _WIN32
    const DWORD drives{GetLogicalDrives()};
    for (int index{}; index < 26; ++index) {
        if ((drives & (1UL << index)) == 0U)
            continue;
        const std::string path{static_cast<char>('A' + index), ':', '\\'};
        locations_.push_back({.kind = ClientFileLocationKind::Drive, .label = path, .path = path});
    }
    std::array<wchar_t, 32'768> profileValue{};
    const DWORD profileSize{GetEnvironmentVariableW(L"USERPROFILE", profileValue.data(), static_cast<DWORD>(profileValue.size()))};
    if (profileSize == 0U || profileSize >= profileValue.size())
        return;
    const std::filesystem::path profile{profileValue.data()};
    const auto append = [&locations = locations_](const ClientFileLocationKind kind, const std::filesystem::path& path) {
        std::error_code error{};
        if (!std::filesystem::is_directory(path, error) || error)
            return;
        const auto labelPath = path.filename();
        locations.push_back({.kind = kind, .label = labelPath.empty() ? Utf8(path) : Utf8(labelPath), .path = Utf8(path)});
    };
    append(ClientFileLocationKind::Home, profile);
    append(ClientFileLocationKind::Desktop, profile / "Desktop");
    append(ClientFileLocationKind::Downloads, profile / "Downloads");
    append(ClientFileLocationKind::Documents, profile / "Documents");
    append(ClientFileLocationKind::Pictures, profile / "Pictures");
    append(ClientFileLocationKind::Music, profile / "Music");
    append(ClientFileLocationKind::Videos, profile / "Videos");
#else
    if (const char* profile = std::getenv("HOME")) {               // NOLINT(gammaray-raw-pointer-boundary): CRT boundary
        locations_.push_back({.kind = ClientFileLocationKind::Home, .label = "Home", .path = profile});
    }
#endif
}

bool ClientLocalFileSystem::LoadComputer(const bool addHistory) {
    RebuildLocations();
    std::vector<ClientLocalEntry> entries{};
    entries.reserve(locations_.size() > 0U ? locations_.size() - 1U : 0U);
    for (const auto& location : locations_) {
        if (location.kind == ClientFileLocationKind::Computer)
            continue;
        entries.push_back({.name = location.label, .path = location.path, .directory = true});
    }
    if (addHistory && !displayPath_.empty() && displayPath_ != "/")
        history_.push_back(path_);
    path_ = std::filesystem::path{"/"};
    displayPath_ = "/";
    entries_ = std::move(entries);
    error_.clear();
    computerView_ = true;
    return true;
}

} // namespace px::client::imgui
