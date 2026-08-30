#ifndef PX_COMMON_NEW_WEAK_CALLBACK_H
#define PX_COMMON_NEW_WEAK_CALLBACK_H

#include <functional>
#include <memory>
#include <utility>

namespace px
{
    template <typename Owner, typename Callback>
    auto MakeWeakVoidCallback(
        std::weak_ptr<Owner> weak_owner,
        Callback callback) {
        return [weak_owner = std::move(weak_owner),
                callback = std::move(callback)](auto&&... args) {
            if (const auto owner = weak_owner.lock()) {
                std::invoke(
                    callback,
                    owner,
                    std::forward<decltype(args)>(args)...);
            }
        };
    }
}

#endif  // PX_COMMON_NEW_WEAK_CALLBACK_H
