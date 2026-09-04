#include "reconnect_backoff.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "async_delay.h"

namespace px {
namespace {

std::uint64_t MakeRandomSeed() {
    std::random_device source;
    const auto high = static_cast<std::uint64_t>(source()) << 32U;
    return high ^ static_cast<std::uint64_t>(source());
}

} // namespace

std::shared_ptr<PxReconnectBackoff> PxReconnectBackoff::Create(PxReconnectBackoffOptions options) {
    if (options.initial_delay < std::chrono::milliseconds::zero() || options.maximum_delay < options.initial_delay ||
        options.multiplier < 1.0 || options.jitter_ratio < 0.0 || options.jitter_ratio > 1.0) {
        return {};
    }
    const auto seed = options.random_seed.value_or(MakeRandomSeed());
    return std::make_shared<PxReconnectBackoff>(std::move(options), seed);
}

PxReconnectBackoff::PxReconnectBackoff(PxReconnectBackoffOptions options, std::uint64_t seed)
    : options_(std::move(options)), random_(seed) {}

std::chrono::milliseconds PxReconnectBackoff::NominalDelay(std::uint32_t attempt) const {
    const auto initial = static_cast<long double>(options_.initial_delay.count());
    const auto maximum = static_cast<long double>(options_.maximum_delay.count());
    const auto exponent = static_cast<long double>(attempt > 0 ? attempt - 1 : 0);
    const auto scaled = initial * std::pow(static_cast<long double>(options_.multiplier), exponent);
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(std::min(scaled, maximum)));
}

PxReconnectBackoffStep PxReconnectBackoff::Next() {
    std::lock_guard lock(mutex_);
    if (attempt_count_ < std::numeric_limits<std::uint32_t>::max()) {
        ++attempt_count_;
    }
    const auto nominal = NominalDelay(attempt_count_);
    const auto jitter_window = static_cast<double>(nominal.count()) * options_.jitter_ratio;
    std::uniform_real_distribution<double> jitter(-jitter_window, jitter_window);
    const auto delayed = std::clamp(static_cast<double>(nominal.count()) + jitter(random_), 0.0,
                                    static_cast<double>(options_.maximum_delay.count()));
    return PxReconnectBackoffStep{.attempt = attempt_count_,
                                  .nominal_delay = nominal,
                                  .delay = std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(std::llround(delayed)))};
}

void PxReconnectBackoff::Reset() {
    std::lock_guard lock(mutex_);
    attempt_count_ = 0;
}

std::uint32_t PxReconnectBackoff::AttemptCount() const {
    std::lock_guard lock(mutex_);
    return attempt_count_;
}

PxAwaitable<PxResult<void>> PxReconnectBackoff::Wait(std::chrono::milliseconds delay) {
    if (delay < std::chrono::milliseconds::zero()) {
        co_return PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "reconnect.wait", "reconnect delay must not be negative"));
    }

    co_return co_await WaitForAsyncDelay(delay, "reconnect.wait");
}

} // namespace px
