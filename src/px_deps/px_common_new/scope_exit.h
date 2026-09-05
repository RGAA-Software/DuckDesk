#ifndef PX_COMMON_NEW_SCOPE_EXIT_H
#define PX_COMMON_NEW_SCOPE_EXIT_H

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace px {

template <std::invocable Function> class PxScopeExit final {
public:
    explicit PxScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>) : function_(std::move(function)) {}

    PxScopeExit(const PxScopeExit&) = delete;
    PxScopeExit& operator=(const PxScopeExit&) = delete;

    PxScopeExit(PxScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<Function>)
        : function_(std::move(other.function_)), active_(std::exchange(other.active_, false)) {}

    PxScopeExit& operator=(PxScopeExit&&) = delete;

    ~PxScopeExit() noexcept {
        if (!active_) return;
        try {
            std::invoke(function_);
        } catch (...) {
            std::terminate();
        }
    }

    void Release() noexcept { active_ = false; }

private:
    Function function_;
    bool active_{true};
};

template <typename Function> PxScopeExit(Function) -> PxScopeExit<Function>;

}  // namespace px

#endif  // PX_COMMON_NEW_SCOPE_EXIT_H
