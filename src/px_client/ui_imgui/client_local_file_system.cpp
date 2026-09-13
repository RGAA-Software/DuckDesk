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
    std::error_code error{};
    const auto initial = std::filesystem::current_path(error);
    static_cast<void>(Load(error ? std::filesystem::path{"C:\\"} : initial, false));
}

const std::string& ClientLocalFileSystem::Path() const noexcept {
    return displayPath_;
}

const std::vector<ClientLocalEntry>& ClientLocalFileSystem::Entries() const noexcept {
    return entries_;
}

const std::string& ClientLocalFileSystem::Error() const noexcept {
    return error_;
}

bool ClientLocalFileSystem::Navigate(const std::string& path) {
    if (path.empty() || path.size() > 4096U)
        return false;
    return Load(std::filesystem::u8path(path), true);
}

bool ClientLocalFileSystem::NavigateUp() {
    const auto parent = path_.parent_path();
    return parent.empty() || parent == path_ ? false : Load(parent, true);
}

bool ClientLocalFileSystem::NavigateBack() {
    if (history_.empty())
        return false;
    const auto previous = history_.back();
    history_.pop_back();
    return Load(previous, false);
}

bool ClientLocalFileSystem::NavigateHome() {
#ifdef _WIN32
    std::array<wchar_t, 32'768> value{};
    const DWORD size{GetEnvironmentVariableW(L"USERPROFILE", value.data(), static_cast<DWORD>(value.size()))};
    return size > 0U && size < value.size() && Load(std::filesystem::path{value.data()}, true);
#else
    return Load(std::filesystem::path{std::getenv("HOME")}, true); // NOLINT(gammaray-raw-pointer-boundary): CRT boundary
#endif
}

bool ClientLocalFileSystem::Refresh() {
    return Load(path_, false);
}

bool ClientLocalFileSystem::CreateDirectory(const std::string& name) {
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
    if (paths.empty())
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
    displayPath_ = Utf8(path_);
    entries_ = std::move(entries);
    error_.clear();
    return true;
}

} // namespace px::client::imgui
