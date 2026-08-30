#ifndef PX_QT_LIFETIME_GUARD_H
#define PX_QT_LIFETIME_GUARD_H

#include <functional>
#include <utility>

#include <QPointer>

namespace px
{

    template<typename Object, typename Callback>
    auto MakeQtLifetimeAction(QPointer<Object> object, Callback callback) {
        return [object, callback = std::move(callback)](auto&&...) mutable {
            if (!object) {
                return;
            }
            std::invoke(callback, object);
        };
    }

}

#endif // PX_QT_LIFETIME_GUARD_H
