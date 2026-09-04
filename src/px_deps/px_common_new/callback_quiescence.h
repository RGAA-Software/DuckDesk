#ifndef PX_COMMON_NEW_CALLBACK_QUIESCENCE_H
#define PX_COMMON_NEW_CALLBACK_QUIESCENCE_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "async_result.h"
#include "async_runtime.h"

namespace px {

class PxCallbackQuiescence final : public std::enable_shared_from_this<PxCallbackQuiescence> {
private:
    class State;

public:
    class Lease final {
    public:
        explicit Lease(std::shared_ptr<State> state);
        ~Lease();

        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept = delete;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

    private:
        void Release() noexcept;

        std::shared_ptr<State> state_{};
    };

    [[nodiscard]] static std::shared_ptr<PxCallbackQuiescence> Create();

    PxCallbackQuiescence();
    ~PxCallbackQuiescence() = default;

    PxCallbackQuiescence(const PxCallbackQuiescence&) = delete;
    PxCallbackQuiescence& operator=(const PxCallbackQuiescence&) = delete;

    [[nodiscard]] std::optional<Lease> TryEnter();
    void BeginStop();
    [[nodiscard]] bool IsAccepting() const;
    [[nodiscard]] std::uint64_t Outstanding() const;
    [[nodiscard]] static PxAwaitable<PxResult<void>> WaitUntilQuiescent(
        const std::shared_ptr<PxCallbackQuiescence>& gate,
        std::chrono::steady_clock::time_point deadline,
        std::string stage);

private:
    std::shared_ptr<State> state_{};
};

} // namespace px

#endif // PX_COMMON_NEW_CALLBACK_QUIESCENCE_H
