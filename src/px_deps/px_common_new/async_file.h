#ifndef PX_COMMON_NEW_ASYNC_FILE_H
#define PX_COMMON_NEW_ASYNC_FILE_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

enum class PxFileAccess {
    kReadOnly,
    kWriteOnly,
    kReadWrite,
};

struct PxFileOpenOptions {
    PxFileAccess access = PxFileAccess::kReadOnly;
    bool create_if_missing = false;
    bool exclusive_create = false;
    bool truncate_existing = false;
    bool sync_on_write = false;
};

using PxByteBuffer = std::vector<std::byte>;

struct PxAsyncFileStatistics {
    std::uint64_t read_operations = 0;
    std::uint64_t write_operations = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t bytes_written = 0;
    std::uint64_t failed_operations = 0;
    std::uint64_t timed_out_operations = 0;
    std::uint64_t active_operations = 0;
};

class PxAsyncFile final {
public:
    static PxAwaitable<PxResult<std::shared_ptr<PxAsyncFile>>> OpenAsync(std::shared_ptr<PxAsyncRuntime> runtime,
                                                                        std::filesystem::path path, PxFileOpenOptions options,
                                                                        std::chrono::steady_clock::time_point deadline);
    static PxAwaitable<PxResult<std::shared_ptr<PxByteBuffer>>> ReadAtAsync(std::shared_ptr<PxAsyncFile> file, std::uint64_t offset,
                                                                           std::size_t size,
                                                                           std::chrono::steady_clock::time_point deadline);
    static PxAwaitable<PxResult<std::size_t>> WriteAtAsync(std::shared_ptr<PxAsyncFile> file, std::uint64_t offset,
                                                           std::shared_ptr<const PxByteBuffer> data,
                                                           std::chrono::steady_clock::time_point deadline);
    static PxAwaitable<PxResult<void>> FlushAsync(std::shared_ptr<PxAsyncFile> file,
                                                  std::chrono::steady_clock::time_point deadline);
    static PxAwaitable<PxResult<void>> CloseAsync(std::shared_ptr<PxAsyncFile> file,
                                                  std::chrono::steady_clock::time_point deadline);

    class State;
    explicit PxAsyncFile(std::shared_ptr<State> state);
    ~PxAsyncFile();

    PxAsyncFile(const PxAsyncFile&) = delete;
    PxAsyncFile& operator=(const PxAsyncFile&) = delete;

    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;
    [[nodiscard]] PxAsyncFileStatistics GetStatistics() const;

private:
    std::shared_ptr<State> state_;
};

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_FILE_H
