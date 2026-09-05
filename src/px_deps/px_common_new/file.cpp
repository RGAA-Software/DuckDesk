#pragma execution_character_set("utf-8")

#include "file.h"

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include "string_util.h"
#include "log.h"

namespace px 
{
    namespace {
        bool SeekFile(const std::unique_ptr<std::FILE, decltype(&std::fclose)>& file, uint64_t offset, int origin) {
            if (!file || offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return false;
            }
#ifdef WIN32
            return _fseeki64(file.get(), static_cast<int64_t>(offset), origin) == 0;
#else
            if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
                return false;
            }
            return fseeko(file.get(), static_cast<off_t>(offset), origin) == 0;
#endif
        }

        std::optional<uint64_t> TellFile(const std::unique_ptr<std::FILE, decltype(&std::fclose)>& file) {
            if (!file) {
                return std::nullopt;
            }
#ifdef WIN32
            const auto offset = _ftelli64(file.get());
#else
            const auto offset = ftello(file.get());
#endif
            if (offset < 0) {
                return std::nullopt;
            }
            return static_cast<uint64_t>(offset);
        }
    }

    std::shared_ptr<File> File::OpenForRead(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "r");
    }
    
    std::shared_ptr<File> File::OpenForWrite(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "w");
    }
    
    std::shared_ptr<File> File::OpenForRW(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "w+");
    }
    
    std::shared_ptr<File> File::OpenForAppend(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "a+");
    }
    
    std::shared_ptr<File> File::OpenForReadB(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "rb");
    }
    
    std::shared_ptr<File> File::OpenForWriteB(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "wb");
    }
    
    std::shared_ptr<File> File::OpenForRWB(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "wb+");
    }
    
    std::shared_ptr<File> File::OpenForAppendB(const std::filesystem::path& path) {
        return std::make_shared<File>(path, "ab+");
    }

    bool File::IsFolder(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    bool File::Exists(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    int64_t File::Size(const std::filesystem::path& path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > static_cast<uintmax_t>(std::numeric_limits<int64_t>::max())) {
            return -1;
        }
        return static_cast<int64_t>(size);
    }

    File::File(std::filesystem::path path, const std::string& mode) : file_path_(std::move(path)) {
#ifdef WIN32
        auto wmode = StringUtil::ToWString(mode);
        fp_.reset(_wfopen(this->file_path_.wstring().c_str(), wmode.c_str()));
#else
        fp_.reset(fopen(file_path_.c_str(), mode.c_str()));
#endif
        if (!fp_) {
            LOGE("Open file failed, mode: {}, file: {}", mode, PathToUTF8(file_path_));
            return;
        }
    }
    
    File::~File() {
        Close();
    }

    bool File::Delete(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    bool File::Exists() const {
        std::error_code ec;
        return std::filesystem::exists(this->file_path_, ec);
    }
    
    bool File::IsOpen() const {
        return static_cast<bool>(fp_);
    }

    std::string File::FileName() const {
        return StringUtil::ToUTF8(file_path_.filename().wstring());
    }

    DataPtr File::Read(uint64_t offset, uint64_t size, uint64_t& read_size) {
        read_size = 0;
        if (!IsOpen()) {
            return nullptr;
        }
        if (!SeekFile(fp_, offset, SEEK_SET)) {
            LOGE("file seek failed, offset: {}, file: {}", offset, StringUtil::ToUTF8(this->file_path_.wstring()));
            return nullptr;
        }
        auto read_data = std::make_unique<char[]>(size);
        read_size = fread(read_data.get(), 1, size, fp_.get());
        if (read_size <= 0) {
            return nullptr;
        }
        return Data::Copy(std::span<const char>{read_data.get(), static_cast<std::size_t>(read_size)});
    }
    
    DataPtr File::ReadAll() {
        uint64_t read_size = 0;
        return Read(0, Size(), read_size);
    }
    
    void File::ReadAll(std::function<bool(uint64_t, DataPtr&&)>&& cbk, int buffer_size) {
        uint64_t offset = 0;
        uint64_t file_size = Size();
        if (buffer_size <= 0) {
            return;
        }
        const auto block_size = static_cast<uint64_t>(buffer_size);
        while (offset < file_size) {
            uint64_t read_size = 0;
            auto data = Read(offset, block_size, read_size);
            if (data && read_size != 0) {
                if (cbk(offset, std::move(data))) {
                    break;
                }
                offset += read_size;
            } else {
                LOGE("read file stopped after an I/O failure, offset: {}, file: {}", offset, StringUtil::ToUTF8(file_path_.wstring()));
                break;
            }
        }
    }
    
    std::string File::ReadAllAsString() {
        auto data = ReadAll();
        if (data) {
            return data->AsString();
        }
        return "";
    }
    
    uint64_t File::Size() const {
        if (!IsOpen()) {
            return 0;
        }
        const auto current = TellFile(fp_);
        if (!current || !SeekFile(fp_, 0, SEEK_END)) {
            return 0;
        }
        const auto size = TellFile(fp_);
        const bool restored = SeekFile(fp_, *current, SEEK_SET);
        return size && restored ? *size : 0;
    }
    
    int64_t File::Write(uint64_t offset, const DataPtr& data) {
        if (!data) {
            return -1;
        }
        return Write(offset, data->Bytes());
    }
    
    int64_t File::Write(uint64_t offset, const std::string& data) {
        return Write(offset, std::span<const char>{data});
    }
    
    int64_t File::Write(uint64_t offset, std::span<const char> data) {
        if (!IsOpen() || data.empty()) {
            return -1;
        }
        if (!SeekFile(fp_, offset, SEEK_SET)) {
            LOGE("seek failed for writing data, offset: {}, file: {}", offset, StringUtil::ToUTF8(file_path_.wstring()));
            return -1;
        }
        return static_cast<int64_t>(fwrite(data.data(), 1, data.size(), fp_.get()));
    }

    int64_t File::Append(const DataPtr& data) {
        if (!data) {
            return -1;
        }
        return Append(data->Bytes());
    }

    int64_t File::Append(const std::string& data) {
        return Append(std::span<const char>{data});
    }

    int64_t File::Append(std::span<const char> data) {
        if (!IsOpen() || data.empty()) {
            return -1;
        }
        return static_cast<int64_t>(fwrite(data.data(), 1, data.size(), fp_.get()));
    }

    void File::Close() {
        if (fp_) {
            fflush(fp_.get());
            fp_.reset();
        }
    }
}
