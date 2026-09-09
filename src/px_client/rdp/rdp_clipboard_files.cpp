#include "rdp_clipboard_files.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <shlobj.h>
#include <sddl.h>

namespace px::rdp {
namespace {
UniqueWinHandle OpenChecked(const QString& path, bool directory, bool write = false) {
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    auto handle = UniqueWinHandle{CreateFileW(native.c_str(),
                                              write       ? GENERIC_WRITE
                                              : directory ? FILE_READ_ATTRIBUTES
                                                          : GENERIC_READ,
                                              FILE_SHARE_READ, nullptr, write ? CREATE_NEW : OPEN_EXISTING,
                                              FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr)};
    BY_HANDLE_FILE_INFORMATION information{};
    if (!handle || handle.get() == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.get(), &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        static_cast<bool>(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory || (!directory && information.nNumberOfLinks != 1)) {
        return {};
    }
    return handle;
}
struct DescriptorCloser final {
    void operator()(void* allocation) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 deleter ABI.
        LocalFree(allocation);
    }
};
bool ProtectStaging(const QString& path) {
    PSECURITY_DESCRIPTOR output{}; // NOLINT(gammaray-raw-pointer-boundary): Win32 allocation out ABI, immediately RAII-owned.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &output, nullptr)) {
        return false;
    }
    const auto descriptor = std::unique_ptr<void, DescriptorCloser>{output};
    return SetFileSecurityW(QDir::toNativeSeparators(path).toStdWString().c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                            descriptor.get()) != FALSE;
}
} // namespace

bool ClipboardFiles::HoldParents(const QString& absolute) {
    // Local fixed disks only: no UNC/network reads on the protocol worker. Hold
    // each ancestor against replacement, not just a pre-open attribute check.
    if (absolute.size() < 3 || absolute.at(1) != ':' || absolute.at(2) != '/' || !QDir::isAbsolutePath(absolute)) {
        return false;
    }
    const auto drive = QDir::toNativeSeparators(absolute.left(3)).toStdWString();
    if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) {
        return false;
    }
    QString current = absolute.left(3);
    const auto parts = absolute.mid(3).split('/');
    if (parts.size() > 64) {
        return false;
    }
    for (qsizetype index{}; index + 1 < parts.size(); ++index) {
        if (parts.at(index).isEmpty() || parts.at(index) == "." || parts.at(index) == "..") {
            return false;
        }
        if (!current.endsWith('/')) {
            current += '/';
        }
        current += parts.at(index);
        if (!held_directories_.contains(current, Qt::CaseInsensitive)) {
            auto held = OpenChecked(current, true);
            if (!held || held_directories_.size() >= 2048) {
                return false;
            }
            held_directories_.push_back(current);
            directory_handles_.push_back(std::move(held));
        }
    }
    return true;
}

bool ClipboardFiles::AddLocal(const QString& absolute, const QString& relative) {
    if (entries_.size() >= kClipboardEntryLimit || !SafeClipboardRelativePath(relative) || !HoldParents(absolute)) {
        return false;
    }
    const QFileInfo info{absolute};
    const bool directory = info.isDir();
    auto handle = OpenChecked(absolute, directory);
    if (!handle) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!directory && (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0 ||
                       static_cast<std::uint64_t>(size.QuadPart) > kClipboardFilesLimit - total_size_)) {
        return false;
    }
    total_size_ += static_cast<std::uint64_t>(size.QuadPart);
    entries_.push_back({relative, static_cast<std::uint64_t>(size.QuadPart), directory});
    handles_.push_back(std::move(handle));
    if (directory) {
        // Do not follow symlinks/reparse points. AddLocal rejects those handles,
        // aborting the whole offer instead of silently providing a partial tree.
        QDirIterator children{absolute, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags};
        while (children.hasNext()) {
            children.next();
            const auto child = children.fileInfo();
            if (!AddLocal(child.absoluteFilePath(), relative + '\\' + child.fileName())) {
                return false;
            }
        }
    }
    return true;
}

std::shared_ptr<ClipboardFiles> ClipboardFiles::Offer(const QList<QUrl>& urls) {
    if (urls.isEmpty() || static_cast<std::size_t>(urls.size()) > kClipboardEntryLimit) {
        return {};
    }
    auto result = std::make_shared<ClipboardFiles>();
    QSet<QString> names{};
    for (const auto& url : urls) {
        if (!url.isLocalFile()) {
            return {};
        }
        const auto absolute = QDir::fromNativeSeparators(QFileInfo{url.toLocalFile()}.absoluteFilePath());
        const auto name = QFileInfo{absolute}.fileName();
        if (names.contains(name.toCaseFolded()) || !result->AddLocal(absolute, name)) {
            return {};
        }
        names.insert(name.toCaseFolded());
        result->roots_.push_back(absolute);
    }
    return result;
}

QByteArray ClipboardFiles::Descriptors() const {
    if (entries_.empty() || entries_.size() > kClipboardEntryLimit) {
        return {};
    }
    QByteArray bytes(4 + static_cast<qsizetype>(entries_.size() * sizeof(FILEDESCRIPTORW)), '\0');
    qToLittleEndian<quint32>(static_cast<quint32>(entries_.size()), bytes.data());
    for (std::size_t index{}; index < entries_.size(); ++index) {
        const auto& entry = entries_.at(index);
        FILEDESCRIPTORW descriptor{}; // Transient Windows wire layout, all padding initialized.
        descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_UNICODE;
        descriptor.dwFileAttributes = entry.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        descriptor.nFileSizeHigh = static_cast<DWORD>(entry.size >> 32);
        descriptor.nFileSizeLow = static_cast<DWORD>(entry.size);
        const auto name = entry.name.toStdWString();
        std::ranges::copy(name, std::begin(descriptor.cFileName));
        std::memcpy(bytes.data() + 4 + index * sizeof(descriptor), &descriptor, sizeof(descriptor));
    }
    return bytes;
}

std::shared_ptr<ClipboardFiles> ClipboardFiles::Receive(std::span<const unsigned char> descriptors) {
    if (descriptors.size() < 4) {
        return {};
    }
    const auto count = qFromLittleEndian<quint32>(descriptors.data());
    if (count == 0 || count > kClipboardEntryLimit || descriptors.size() != 4 + count * sizeof(FILEDESCRIPTORW)) {
        return {};
    }
    auto result = std::make_shared<ClipboardFiles>();
    QSet<QString> names{};
    QSet<QString> directories{};
    for (std::size_t index{}; index < count; ++index) {
        FILEDESCRIPTORW descriptor{};
        std::memcpy(&descriptor, descriptors.data() + 4 + index * sizeof(descriptor), sizeof(descriptor));
        const auto characters = std::span<const wchar_t>{descriptor.cFileName};
        const auto end = std::ranges::find(characters, L'\0');
        if (end == characters.end()) {
            return {};
        }
        const auto name = QString::fromWCharArray(characters.data(), static_cast<qsizetype>(end - characters.begin()));
        const auto folded = name.toCaseFolded();
        if (!SafeClipboardRelativePath(name) || names.contains(folded) || !(descriptor.dwFlags & FD_ATTRIBUTES) ||
            (descriptor.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))) {
            return {};
        }
        const bool directory = (descriptor.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::uint64_t size = (static_cast<std::uint64_t>(descriptor.nFileSizeHigh) << 32) | descriptor.nFileSizeLow;
        if ((!directory && !(descriptor.dwFlags & FD_FILESIZE)) || (directory && size != 0) || size > kClipboardFilesLimit - result->total_size_) {
            return {};
        }
        const auto separator = folded.lastIndexOf('\\');
        if (separator >= 0 && !directories.contains(folded.left(separator))) {
            return {};
        }
        names.insert(folded);
        if (directory) {
            directories.insert(folded);
        }
        result->total_size_ += size;
        result->entries_.push_back({name, size, directory});
    }
    result->staging_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/GammaRayRdpClipboard-XXXXXX"));
    if (!result->staging_->isValid() || !ProtectStaging(result->staging_->path())) {
        return {};
    }
    auto root_handle = OpenChecked(result->staging_->path(), true);
    if (!root_handle) {
        return {};
    }
    result->directory_handles_.push_back(std::move(root_handle));
    for (const auto& entry : result->entries_) {
        const auto path = result->staging_->path() + '/' + QString{entry.name}.replace('\\', '/');
        if (entry.directory && !CreateDirectoryW(QDir::toNativeSeparators(path).toStdWString().c_str(), nullptr)) {
            return {};
        }
        auto handle = OpenChecked(path, entry.directory, !entry.directory);
        if (!handle) {
            return {};
        }
        result->handles_.push_back(std::move(handle));
        if (!entry.name.contains('\\')) {
            result->roots_.push_back(path);
        }
    }
    result->received_.resize(count, 0);
    return result;
}

QByteArray ClipboardFiles::Read(std::size_t index, std::uint64_t offset, std::uint32_t length) const {
    if (staging_ || index >= entries_.size() || length > 64 * 1024 || entries_.at(index).directory || offset > entries_.at(index).size) {
        return {};
    }
    const auto count = static_cast<DWORD>(std::min<std::uint64_t>(length, entries_.at(index).size - offset));
    QByteArray bytes(count, '\0');
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read{};
    if (!SetFilePointerEx(handles_.at(index).get(), position, nullptr, FILE_BEGIN) ||
        (count != 0 && (!ReadFile(handles_.at(index).get(), bytes.data(), count, &read, nullptr) || read != count))) {
        return {};
    }
    return bytes;
}

bool ClipboardFiles::Write(std::size_t index, std::uint64_t offset, std::span<const unsigned char> bytes) {
    if (!staging_ || published_ || index >= entries_.size() || entries_.at(index).directory || bytes.size() > 64 * 1024 ||
        received_.at(index) != offset || offset > entries_.at(index).size || bytes.size() > entries_.at(index).size - offset) {
        return false;
    }
    DWORD written{};
    if (!WriteFile(handles_.at(index).get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size()) {
        return false;
    }
    received_.at(index) += written;
    return true;
}

QList<QUrl> ClipboardFiles::Publish() {
    if (!staging_ || published_) {
        return {};
    }
    for (std::size_t index{}; index < entries_.size(); ++index) {
        if (received_.at(index) != entries_.at(index).size || (!entries_.at(index).directory && !FlushFileBuffers(handles_.at(index).get()))) {
            return {};
        }
    }
    // Windows Explorer opens with sharing compatible with immutable read offers,
    // not outstanding GENERIC_WRITE handles. Reopen without allowing mutation.
    handles_.clear();
    for (const auto& entry : entries_) {
        const auto path = staging_->path() + '/' + QString{entry.name}.replace('\\', '/');
        auto handle = OpenChecked(path, entry.directory);
        if (!handle) {
            return {};
        }
        handles_.push_back(std::move(handle));
    }
    QList<QUrl> urls{};
    for (const auto& root : roots_) {
        urls.push_back(QUrl::fromLocalFile(root));
    }
    published_ = true;
    return urls;
}

} // namespace px::rdp
