#pragma execution_character_set("utf-8")

#include "file.h"

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include "string_util.h"
#include "log.h"

namespace tc 
{
    std::shared_ptr<File> File::OpenForRead(const U8Path& path) {
        return std::make_shared<File>(path, "r");
    }
    
    std::shared_ptr<File> File::OpenForWrite(const U8Path& path) {
        return std::make_shared<File>(path, "w");
    }
    
    std::shared_ptr<File> File::OpenForRW(const U8Path& path) {
        return std::make_shared<File>(path, "w+");
    }
    
    std::shared_ptr<File> File::OpenForAppend(const U8Path& path) {
        return std::make_shared<File>(path, "a+");
    }
    
    std::shared_ptr<File> File::OpenForReadB(const U8Path& path) {
        return std::make_shared<File>(path, "rb");
    }
    
    std::shared_ptr<File> File::OpenForWriteB(const U8Path& path) {
        return std::make_shared<File>(path, "wb");
    }
    
    std::shared_ptr<File> File::OpenForRWB(const U8Path& path) {
        return std::make_shared<File>(path, "wb+");
    }
    
    std::shared_ptr<File> File::OpenForAppendB(const U8Path& path) {
        return std::make_shared<File>(path, "ab+");
    }

    bool File::IsFolder(const U8Path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    bool File::Exists(const U8Path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    int64_t File::Size(const U8Path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return -1;
        }
        return static_cast<int64_t>(std::filesystem::file_size(path, ec));
    }

    File::File(const U8Path& path, const std::string& mode) {
        this->file_path_ = path;
#ifdef WIN32
        auto wmode = StringUtil::ToWString(mode);
        fp_ = _wfopen(this->file_path_.wstring().c_str(), wmode.c_str());
#else
        fp_ = fopen(path.c_str(), mode.c_str());
#endif
        if (!fp_) {
            LOGE("Open file failed, mode: {}, file: {}", mode, StringUtil::ToUTF8(path.wstring()));
            return;
        }
    }
    
    File::~File() {
        Close();
    }

    bool File::Delete(const U8Path& path) {
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    bool File::Exists() {
        std::error_code ec;
        return std::filesystem::exists(this->file_path_, ec);
    }
    
    bool File::IsOpen() {
        return fp_ != nullptr;
    }

    std::string File::FileName() {
        return StringUtil::ToUTF8(file_path_.filename().wstring());
    }

    DataPtr File::Read(uint64_t offset, uint64_t size, uint64_t& read_size) {
        if (!IsOpen()) {
            return nullptr;
        }
        if (fseek(fp_, static_cast<long>(offset), SEEK_SET) != 0) {
            LOGE("file seek failed, offset: {}, file: {}", offset, StringUtil::ToUTF8(this->file_path_.wstring()));
            return nullptr;
        }
        auto read_data = std::make_unique<char[]>(size);
        read_size = fread(read_data.get(), 1, size, fp_);
        if (read_size <= 0) {
            return nullptr;
        }
        return Data::Make(read_data.get(), static_cast<int>(read_size));
    }
    
    DataPtr File::ReadAll() {
        uint64_t read_size;
        return Read(0, Size(), read_size);
    }
    
    void File::ReadAll(std::function<bool(uint64_t, DataPtr&&)>&& cbk, int buffer_size) {
        uint64_t offset = 0;
        uint64_t file_size = Size();
        uint32_t block_size = buffer_size;
        while (offset < file_size) {
            uint64_t read_size = 0;
            auto data = Read(offset, block_size, read_size);
            if (data && read_size != 0) {
                if (cbk(offset, std::move(data))) {
                    break;
                }
                offset += read_size;
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
    
    uint64_t File::Size() {
        if (!IsOpen()) {
            return 0;
        }
        auto current = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        auto size = ftell(fp_);
        fseek(fp_, current, SEEK_SET);
        return static_cast<uint64_t>(size);
    }
    
    int64_t File::Write(uint64_t offset, const DataPtr& data) {
        if (!data) {
            return -1;
        }
        return Write(offset, data->CStr(), data->Size());
    }
    
    int64_t File::Write(uint64_t offset, const std::string& data) {
        if (data.empty()) {
            return -1;
        }
        return Write(offset, data.c_str(), data.size());
    }
    
    int64_t File::Write(uint64_t offset, const char* data, uint64_t size) {
        if (!IsOpen()) {
            return -1;
        }
        if (fseek(fp_, static_cast<long>(offset), SEEK_SET) != 0) {
            LOGE("seek failed for writing data, offset: {}, file: {}", offset, StringUtil::ToUTF8(file_path_.wstring()));
            return -1;
        }
        return static_cast<int64_t>(fwrite(data, 1, size, fp_));
    }

    int64_t File::Append(const DataPtr& data) {
        return this->Append(data->CStr(), data->Size());
    }

    int64_t File::Append(const std::string& data) {
        return this->Append(data.c_str(), data.size());
    }

    int64_t File::Append(const char* data, uint64_t size) {
        if (!IsOpen()) {
            return -1;
        }
        return static_cast<int64_t>(fwrite(data, 1, size, fp_));
    }

    void File::Close() {
        if (fp_) {
            fflush(fp_);
            fclose(fp_);
            fp_ = nullptr;
        }
    }
}
