#include "async_file.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <asio/cancel_after.hpp>

#include "async_blocking_call.h"
#include "async_operation.h"
#include "path_codec.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace px {
namespace {

constexpr std::size_t kMaxSingleFileOperationBytes = 256ULL * 1024ULL * 1024ULL;

enum class FileLifecycle {
    kOpen,
    kClosing,
    kClosed,
    kFailed,
};

PxAsyncError MakeFileError(PxAsyncErrorCode code, std::string stage, std::string message, bool retryable = false,
                           std::string detail_code = {}) {
    return MakePxAsyncError(code, std::move(stage), std::move(message), retryable, std::move(detail_code));
}

PxBlockingTaskPoster MakeBlockingPoster(const std::shared_ptr<PxAsyncRuntime>& runtime) {
    return [runtime](std::function<void()> task) {
        if (!runtime->DeferBlocking(std::move(task))) {
            throw std::runtime_error("blocking executor rejected file operation");
        }
    };
}

#if defined(_WIN32)
asio::file_base::flags ToAsioFlags(const PxFileOpenOptions& options) {
    auto flags = asio::file_base::read_only;
    switch (options.access) {
    case PxFileAccess::kReadOnly:
        flags = asio::file_base::read_only;
        break;
    case PxFileAccess::kWriteOnly:
        flags = asio::file_base::write_only;
        break;
    case PxFileAccess::kReadWrite:
        flags = asio::file_base::read_write;
        break;
    }
    if (options.create_if_missing) {
        flags |= asio::file_base::create;
    }
    if (options.exclusive_create) {
        flags |= asio::file_base::exclusive;
    }
    if (options.truncate_existing) {
        flags |= asio::file_base::truncate;
    }
    if (options.sync_on_write) {
        flags |= asio::file_base::sync_all_on_write;
    }
    return flags;
}
#else
class NativeFile final {
public:
    NativeFile() = default;
    ~NativeFile() {
        Close();
    }

    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;

    void Open(const std::filesystem::path& path, const PxFileOpenOptions& options) {
        int flags = O_RDONLY;
        switch (options.access) {
        case PxFileAccess::kReadOnly:
            flags = O_RDONLY;
            break;
        case PxFileAccess::kWriteOnly:
            flags = O_WRONLY;
            break;
        case PxFileAccess::kReadWrite:
            flags = O_RDWR;
            break;
        }
        flags |= options.create_if_missing ? O_CREAT : 0;
        flags |= options.exclusive_create ? O_EXCL : 0;
        flags |= options.truncate_existing ? O_TRUNC : 0;
        flags |= options.sync_on_write ? O_SYNC : 0;
        descriptor_ = ::open(path.c_str(), flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (descriptor_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open");
        }
    }

    [[nodiscard]] int Descriptor() const noexcept {
        return descriptor_;
    }

    void Close() noexcept {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
            descriptor_ = -1;
        }
    }

private:
    int descriptor_ = -1;
};
#endif

} // namespace

class PxAsyncFile::State final {
public:
    State(std::shared_ptr<PxAsyncRuntime> runtime_value, std::filesystem::path path_value, PxFileOpenOptions options_value)
        : runtime(std::move(runtime_value)), strand(asio::make_strand(runtime->Executor(PxAsyncLane::kWorker))), path(std::move(path_value)),
          options(options_value)
#if defined(_WIN32)
          , file(std::make_shared<asio::random_access_file>(strand))
#endif
    {}

    std::shared_ptr<PxAsyncRuntime> runtime;
    asio::strand<asio::any_io_executor> strand;
    std::filesystem::path path;
    PxFileOpenOptions options{};
    mutable std::mutex mutex;
    FileLifecycle lifecycle = FileLifecycle::kFailed;
    PxAsyncFileStatistics statistics{};
    std::shared_ptr<PxAsyncOneShot<void>> close_completion;
#if defined(_WIN32)
    std::shared_ptr<asio::random_access_file> file;
#else
    NativeFile file;
    std::mutex native_io_mutex;
#endif
};

namespace {

bool BeginOperation(const std::shared_ptr<PxAsyncFile::State>& state) {
    std::lock_guard lock(state->mutex);
    if (state->lifecycle != FileLifecycle::kOpen) {
        return false;
    }
    ++state->statistics.active_operations;
    return true;
}

void FinishOperation(const std::shared_ptr<PxAsyncFile::State>& state, bool write, std::size_t bytes, std::optional<PxAsyncError> error) {
    std::shared_ptr<PxAsyncOneShot<void>> close_completion;
    {
        std::lock_guard lock(state->mutex);
        if (write) {
            ++state->statistics.write_operations;
            state->statistics.bytes_written += bytes;
        } else {
            ++state->statistics.read_operations;
            state->statistics.bytes_read += bytes;
        }
        if (error) {
            ++state->statistics.failed_operations;
            state->statistics.timed_out_operations += error->code == PxAsyncErrorCode::kTimeout ? 1 : 0;
        }
        --state->statistics.active_operations;
        if (state->lifecycle == FileLifecycle::kClosing && state->statistics.active_operations == 0) {
            close_completion = state->close_completion;
        }
    }
    if (close_completion) {
        static_cast<void>(close_completion->TryComplete(PxResult<void>::Success()));
    }
}

PxAsyncError ErrorFromIo(std::string stage, const asio::error_code& error, std::chrono::steady_clock::time_point deadline) {
    if (error == asio::error::operation_aborted) {
        return std::chrono::steady_clock::now() >= deadline
            ? MakeFileError(PxAsyncErrorCode::kTimeout, std::move(stage), "file operation timed out", true, "FILE_TIMEOUT")
            : MakeFileError(PxAsyncErrorCode::kCancelled, std::move(stage), "file operation was cancelled", true, "FILE_CANCELLED");
    }
    return MakeFileError(PxAsyncErrorCode::kIoError, std::move(stage), error.message(), true, std::to_string(error.value()));
}

} // namespace

PxAsyncFile::PxAsyncFile(std::shared_ptr<State> state) : state_(std::move(state)) {}

PxAsyncFile::~PxAsyncFile() = default;

PxAwaitable<PxResult<std::shared_ptr<PxAsyncFile>>> PxAsyncFile::OpenAsync(std::shared_ptr<PxAsyncRuntime> runtime,
                                                                           std::filesystem::path path, PxFileOpenOptions options,
                                                                           std::chrono::steady_clock::time_point deadline) {
    if (!runtime || path.empty()) {
        co_return PxResult<std::shared_ptr<PxAsyncFile>>::Failure(
            MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.open", "runtime and path are required"));
    }
    if (options.exclusive_create && !options.create_if_missing) {
        co_return PxResult<std::shared_ptr<PxAsyncFile>>::Failure(
            MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.open", "exclusive_create requires create_if_missing"));
    }

    const auto completion_executor = co_await asio::this_coro::executor;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto opened = co_await AwaitBlockingCall<std::shared_ptr<State>>(
        MakeBlockingPoster(runtime), completion_executor, deadline, cancellation, "file.open",
        [runtime, path = std::move(path), options](const auto&) {
            const auto state = std::make_shared<State>(runtime, path, options);
#if defined(_WIN32)
            const auto utf8_path = PathToUtf8(path);
            if (!utf8_path) {
                throw std::invalid_argument(utf8_path.Error().message);
            }
            asio::error_code error;
            state->file->open(utf8_path.Value(), ToAsioFlags(options), error);
            if (error) {
                throw asio::system_error(error, "open");
            }
#else
            state->file.Open(path, options);
#endif
            {
                std::lock_guard lock(state->mutex);
                state->lifecycle = FileLifecycle::kOpen;
            }
            return state;
        }, PxAsyncErrorCode::kIoError);
    if (!opened) {
        co_return PxResult<std::shared_ptr<PxAsyncFile>>::Failure(opened.Error());
    }
    co_return PxResult<std::shared_ptr<PxAsyncFile>>::Success(std::make_shared<PxAsyncFile>(opened.TakeValue()));
}

PxAwaitable<PxResult<std::shared_ptr<PxByteBuffer>>> PxAsyncFile::ReadAtAsync(std::shared_ptr<PxAsyncFile> file, std::uint64_t offset,
                                                                              std::size_t size,
                                                                              std::chrono::steady_clock::time_point deadline) {
    if (!file || size > kMaxSingleFileOperationBytes) {
        co_return PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
            MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.read", "file is required and read size must not exceed 256 MiB"));
    }
    const auto state = file->state_;
    if (!BeginOperation(state)) {
        co_return PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
            MakeFileError(PxAsyncErrorCode::kServiceStopped, "file.read", "file is closing or closed"));
    }
    if (size == 0) {
        FinishOperation(state, false, 0, std::nullopt);
        co_return PxResult<std::shared_ptr<PxByteBuffer>>::Success(std::make_shared<PxByteBuffer>());
    }

    PxResult<std::shared_ptr<PxByteBuffer>> result = PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
        MakeFileError(PxAsyncErrorCode::kIoError, "file.read", "file read did not complete"));
#if defined(_WIN32)
    try {
        auto scheduled_result = co_await asio::co_spawn(
            state->strand,
            [state, offset, size, deadline]() -> PxAwaitable<std::optional<PxResult<std::shared_ptr<PxByteBuffer>>>> {
                if (std::chrono::steady_clock::now() >= deadline) {
                    co_return PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
                        MakeFileError(PxAsyncErrorCode::kTimeout, "file.read", "file read deadline expired", true, "FILE_TIMEOUT"));
                }
                const auto data = std::make_shared<PxByteBuffer>(size);
                asio::error_code error;
                const auto transferred = co_await state->file->async_read_some_at(
                    offset, asio::buffer(*data), asio::cancel_after(deadline - std::chrono::steady_clock::now(),
                                                                   asio::redirect_error(asio::use_awaitable, error)));
                if (error && error != asio::error::eof) {
                    co_return PxResult<std::shared_ptr<PxByteBuffer>>::Failure(ErrorFromIo("file.read", error, deadline));
                }
                data->resize(transferred);
                co_return PxResult<std::shared_ptr<PxByteBuffer>>::Success(data);
            },
            asio::use_awaitable);
        if (scheduled_result) {
            result = std::move(*scheduled_result);
        }
    } catch (const std::exception& error) {
        result = PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
            MakeFileError(PxAsyncErrorCode::kIoError, "file.read", error.what(), true));
    } catch (...) {
        result = PxResult<std::shared_ptr<PxByteBuffer>>::Failure(
            MakeFileError(PxAsyncErrorCode::kIoError, "file.read", "file read threw a non-standard exception", true));
    }
#else
    const auto completion_executor = co_await asio::this_coro::executor;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    result = co_await AwaitBlockingCall<std::shared_ptr<PxByteBuffer>>(
        MakeBlockingPoster(state->runtime), completion_executor, deadline, cancellation, "file.read",
        [state, offset, size](const auto&) {
            if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
                throw std::out_of_range("file read offset exceeds platform range");
            }
            const auto data = std::make_shared<PxByteBuffer>(size);
            std::lock_guard lock(state->native_io_mutex);
            const auto transferred = ::pread(state->file.Descriptor(), data->data(), size, static_cast<off_t>(offset));
            if (transferred < 0) {
                throw std::system_error(errno, std::generic_category(), "pread");
            }
            data->resize(static_cast<std::size_t>(transferred));
            return data;
        }, PxAsyncErrorCode::kIoError);
#endif
    if (!result) {
        const auto error = result.Error();
        FinishOperation(state, false, 0, error);
        co_return PxResult<std::shared_ptr<PxByteBuffer>>::Failure(error);
    }
    const auto bytes = result.Value()->size();
    FinishOperation(state, false, bytes, std::nullopt);
    co_return result;
}

PxAwaitable<PxResult<std::size_t>> PxAsyncFile::WriteAtAsync(std::shared_ptr<PxAsyncFile> file, std::uint64_t offset,
                                                              std::shared_ptr<const PxByteBuffer> data,
                                                              std::chrono::steady_clock::time_point deadline) {
    if (!file || !data || data->size() > kMaxSingleFileOperationBytes) {
        co_return PxResult<std::size_t>::Failure(
            MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.write", "file and data are required and write size must not exceed 256 MiB"));
    }
    const auto state = file->state_;
    if (!BeginOperation(state)) {
        co_return PxResult<std::size_t>::Failure(MakeFileError(PxAsyncErrorCode::kServiceStopped, "file.write", "file is closing or closed"));
    }
    if (data->empty()) {
        FinishOperation(state, true, 0, std::nullopt);
        co_return PxResult<std::size_t>::Success(0);
    }

    PxResult<std::size_t> result = PxResult<std::size_t>::Failure(
        MakeFileError(PxAsyncErrorCode::kIoError, "file.write", "file write did not complete"));
#if defined(_WIN32)
    try {
        auto scheduled_result = co_await asio::co_spawn(
            state->strand,
            [state, offset, data, deadline]() -> PxAwaitable<std::optional<PxResult<std::size_t>>> {
                if (std::chrono::steady_clock::now() >= deadline) {
                    co_return PxResult<std::size_t>::Failure(
                        MakeFileError(PxAsyncErrorCode::kTimeout, "file.write", "file write deadline expired", true, "FILE_TIMEOUT"));
                }
                asio::error_code error;
                const auto transferred = co_await state->file->async_write_some_at(
                    offset, asio::buffer(*data), asio::cancel_after(deadline - std::chrono::steady_clock::now(),
                                                                   asio::redirect_error(asio::use_awaitable, error)));
                if (error) {
                    co_return PxResult<std::size_t>::Failure(ErrorFromIo("file.write", error, deadline));
                }
                co_return PxResult<std::size_t>::Success(transferred);
            },
            asio::use_awaitable);
        if (scheduled_result) {
            result = std::move(*scheduled_result);
        }
    } catch (const std::exception& error) {
        result = PxResult<std::size_t>::Failure(MakeFileError(PxAsyncErrorCode::kIoError, "file.write", error.what(), true));
    } catch (...) {
        result = PxResult<std::size_t>::Failure(
            MakeFileError(PxAsyncErrorCode::kIoError, "file.write", "file write threw a non-standard exception", true));
    }
#else
    const auto completion_executor = co_await asio::this_coro::executor;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    result = co_await AwaitBlockingCall<std::size_t>(
        MakeBlockingPoster(state->runtime), completion_executor, deadline, cancellation, "file.write",
        [state, offset, data](const auto&) {
            if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
                throw std::out_of_range("file write offset exceeds platform range");
            }
            std::lock_guard lock(state->native_io_mutex);
            const auto transferred = ::pwrite(state->file.Descriptor(), data->data(), data->size(), static_cast<off_t>(offset));
            if (transferred < 0) {
                throw std::system_error(errno, std::generic_category(), "pwrite");
            }
            return static_cast<std::size_t>(transferred);
        }, PxAsyncErrorCode::kIoError);
#endif
    if (!result) {
        const auto error = result.Error();
        FinishOperation(state, true, 0, error);
        co_return PxResult<std::size_t>::Failure(error);
    }
    FinishOperation(state, true, result.Value(), std::nullopt);
    co_return result;
}

PxAwaitable<PxResult<void>> PxAsyncFile::FlushAsync(std::shared_ptr<PxAsyncFile> file,
                                                     std::chrono::steady_clock::time_point deadline) {
    if (!file) {
        co_return PxResult<void>::Failure(MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.flush", "file is required"));
    }
    const auto state = file->state_;
    if (!BeginOperation(state)) {
        co_return PxResult<void>::Failure(MakeFileError(PxAsyncErrorCode::kServiceStopped, "file.flush", "file is closing or closed"));
    }
    const auto completion_executor = co_await asio::this_coro::executor;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto result = co_await AwaitBlockingCall<bool>(
        MakeBlockingPoster(state->runtime), completion_executor, deadline, cancellation, "file.flush",
        [state](const auto&) {
#if defined(_WIN32)
            asio::error_code error;
            state->file->sync_all(error);
            if (error) {
                throw asio::system_error(error, "sync_all");
            }
#else
            std::lock_guard lock(state->native_io_mutex);
            if (::fsync(state->file.Descriptor()) != 0) {
                throw std::system_error(errno, std::generic_category(), "fsync");
            }
#endif
            return true;
        }, PxAsyncErrorCode::kIoError);
    if (!result) {
        const auto error = result.Error();
        FinishOperation(state, true, 0, error);
        co_return PxResult<void>::Failure(error);
    }
    FinishOperation(state, true, 0, std::nullopt);
    co_return PxResult<void>::Success();
}

PxAwaitable<PxResult<void>> PxAsyncFile::CloseAsync(std::shared_ptr<PxAsyncFile> file,
                                                     std::chrono::steady_clock::time_point deadline) {
    if (!file) {
        co_return PxResult<void>::Failure(MakeFileError(PxAsyncErrorCode::kInvalidArgument, "file.close", "file is required"));
    }
    const auto state = file->state_;
    std::shared_ptr<PxAsyncOneShot<void>> completion;
    {
        std::lock_guard lock(state->mutex);
        if (state->lifecycle == FileLifecycle::kClosed) {
            co_return PxResult<void>::Success();
        }
        if (state->lifecycle == FileLifecycle::kFailed) {
            co_return PxResult<void>::Failure(MakeFileError(PxAsyncErrorCode::kIoError, "file.close", "file is in failed state"));
        }
        state->lifecycle = FileLifecycle::kClosing;
        if (state->statistics.active_operations > 0) {
            if (!state->close_completion) {
                state->close_completion = PxAsyncOneShot<void>::Create(state->strand);
            }
            completion = state->close_completion;
        }
    }

#if defined(_WIN32)
    co_await asio::co_spawn(
        state->strand,
        [state]() -> PxAwaitable<void> {
            asio::error_code ignored;
            state->file->cancel(ignored);
            state->file->close(ignored);
            co_return;
        },
        asio::use_awaitable);
#endif
    if (completion) {
        const auto drained = co_await PxAsyncOneShot<void>::WaitUntil(completion, deadline);
        if (!drained) {
            co_return PxResult<void>::Failure(drained.Error());
        }
    }

#if !defined(_WIN32)
    const auto completion_executor = co_await asio::this_coro::executor;
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto closed = co_await AwaitBlockingCall<bool>(
        MakeBlockingPoster(state->runtime), completion_executor, deadline, cancellation, "file.close",
        [state](const auto&) {
            std::lock_guard lock(state->native_io_mutex);
            state->file.Close();
            return true;
        }, PxAsyncErrorCode::kIoError);
    if (!closed) {
        co_return PxResult<void>::Failure(closed.Error());
    }
#endif
    {
        std::lock_guard lock(state->mutex);
        state->lifecycle = FileLifecycle::kClosed;
        state->close_completion.reset();
    }
    co_return PxResult<void>::Success();
}

bool PxAsyncFile::IsOpen() const {
    std::lock_guard lock(state_->mutex);
    return state_->lifecycle == FileLifecycle::kOpen;
}

const std::filesystem::path& PxAsyncFile::Path() const noexcept {
    return state_->path;
}

PxAsyncFileStatistics PxAsyncFile::GetStatistics() const {
    std::lock_guard lock(state_->mutex);
    return state_->statistics;
}

} // namespace px
