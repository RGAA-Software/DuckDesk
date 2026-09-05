#ifndef FILE_H
#define FILE_H

#include <functional>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <span>
#include "data.h"

namespace px 
{

    class File {
    public:
    
        static std::shared_ptr<File> OpenForRead(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForWrite(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForRW(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForAppend(const std::filesystem::path& path);
    
        static std::shared_ptr<File> OpenForReadB(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForWriteB(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForRWB(const std::filesystem::path& path);
        static std::shared_ptr<File> OpenForAppendB(const std::filesystem::path& path);

        static bool IsFolder(const std::filesystem::path& path);
        static bool Exists(const std::filesystem::path& path);
        static int64_t Size(const std::filesystem::path& path);

        File(std::filesystem::path path, const std::string& mode);
        ~File();
        File(const File&) = delete;
        File& operator=(const File&) = delete;
        File(File&&) noexcept = default;
        File& operator=(File&&) noexcept = default;
        static bool Delete(const std::filesystem::path& path);
        [[nodiscard]] uint64_t Size() const;
        [[nodiscard]] bool Exists() const;
        [[nodiscard]] bool IsOpen() const;
        void Close();
        [[nodiscard]] std::string FileName() const;

        DataPtr Read(uint64_t offset, uint64_t size, uint64_t& read_size);
        DataPtr ReadAll();
        void ReadAll(std::function<bool(uint64_t offset, DataPtr&&)>&& cbk, int buffer_size = 4096);
        std::string ReadAllAsString();
    
        int64_t Write(uint64_t offset, const DataPtr& data);
        int64_t Write(uint64_t offset, const std::string& data);
        int64_t Write(uint64_t offset, std::span<const char> data);
        template <std::size_t Size>
        int64_t Write(uint64_t offset, const char (&data)[Size]) {
            static_assert(Size > 0);
            return Write(offset, std::span<const char>{data, Size - 1});
        }
        int64_t Append(const DataPtr& data);
        int64_t Append(const std::string& data);
        int64_t Append(std::span<const char> data);
        template <std::size_t Size>
        int64_t Append(const char (&data)[Size]) {
            static_assert(Size > 0);
            return Append(std::span<const char>{data, Size - 1});
        }

    private:
        using FileHandle = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
        std::filesystem::path file_path_;
        FileHandle fp_{nullptr, &std::fclose};
    };
    
    typedef std::shared_ptr<File> FilePtr;

}

#endif // FILE_H
