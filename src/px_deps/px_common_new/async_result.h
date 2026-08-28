#ifndef PX_COMMON_NEW_ASYNC_RESULT_H
#define PX_COMMON_NEW_ASYNC_RESULT_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace px {

enum class PxAsyncErrorCode {
    kInvalidArgument,
    kRequestInProgress,
    kServiceNotConnected,
    kQueueFull,
    kTimeout,
    kCancelled,
    kServiceStopped,
    kProtocolError,
    kServiceRejected,
};

inline std::string_view PxAsyncErrorCodeName(PxAsyncErrorCode code) noexcept {
    switch (code) {
    case PxAsyncErrorCode::kInvalidArgument:
        return "INVALID_ARGUMENT";
    case PxAsyncErrorCode::kRequestInProgress:
        return "REQUEST_IN_PROGRESS";
    case PxAsyncErrorCode::kServiceNotConnected:
        return "SERVICE_NOT_CONNECTED";
    case PxAsyncErrorCode::kQueueFull:
        return "QUEUE_FULL";
    case PxAsyncErrorCode::kTimeout:
        return "TIMEOUT";
    case PxAsyncErrorCode::kCancelled:
        return "CANCELLED";
    case PxAsyncErrorCode::kServiceStopped:
        return "SERVICE_STOPPED";
    case PxAsyncErrorCode::kProtocolError:
        return "PROTOCOL_ERROR";
    case PxAsyncErrorCode::kServiceRejected:
        return "SERVICE_REJECTED";
    }
    return "PROTOCOL_ERROR";
}

struct PxAsyncError {
    PxAsyncErrorCode code = PxAsyncErrorCode::kProtocolError;
    std::string stage;
    std::string message;
    std::string detail_code;
    bool retryable = false;

    [[nodiscard]] std::string StableCode() const {
        return detail_code.empty() ? std::string(PxAsyncErrorCodeName(code)) : detail_code;
    }
};

template<typename T>
class PxResult final {
public:
    static PxResult Success(T value) {
        return PxResult(std::in_place_index<0>, std::move(value));
    }

    static PxResult Failure(PxAsyncError error) {
        return PxResult(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool HasValue() const noexcept {
        return value_.index() == 0;
    }

    explicit operator bool() const noexcept {
        return HasValue();
    }

    [[nodiscard]] const T& Value() const {
        return std::get<0>(value_);
    }

    [[nodiscard]] T TakeValue() {
        return std::move(std::get<0>(value_));
    }

    [[nodiscard]] const PxAsyncError& Error() const {
        return std::get<1>(value_);
    }

private:
    template<std::size_t Index, typename Value>
    explicit PxResult(std::in_place_index_t<Index> index, Value&& value)
        : value_(index, std::forward<Value>(value)) {}

    std::variant<T, PxAsyncError> value_;
};

template<>
class PxResult<void> final {
public:
    static PxResult Success() {
        return PxResult(true, {});
    }

    static PxResult Failure(PxAsyncError error) {
        return PxResult(false, std::move(error));
    }

    [[nodiscard]] bool HasValue() const noexcept {
        return success_;
    }

    explicit operator bool() const noexcept {
        return HasValue();
    }

    [[nodiscard]] const PxAsyncError& Error() const {
        if (success_) {
            throw std::logic_error("successful PxResult<void> has no error");
        }
        return error_;
    }

private:
    PxResult(bool success, PxAsyncError error)
        : success_(success), error_(std::move(error)) {}

    bool success_ = false;
    PxAsyncError error_;
};

inline PxAsyncError MakePxAsyncError(PxAsyncErrorCode code,
                                     std::string stage,
                                     std::string message,
                                     bool retryable = false,
                                     std::string detail_code = {}) {
    return PxAsyncError{
        .code = code,
        .stage = std::move(stage),
        .message = std::move(message),
        .detail_code = std::move(detail_code),
        .retryable = retryable,
    };
}

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_RESULT_H
