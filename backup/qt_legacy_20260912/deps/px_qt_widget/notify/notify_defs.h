//
// Created by RGAA on 25/06/2025.
//

#ifndef PX_NOTIFY_DEFS_H
#define PX_NOTIFY_DEFS_H

#include <QString>
#include <functional>

namespace px
{

    enum class NotifyItemType {
        kNormal,
        kError,
        kWarning,
    };

    class NotifyItem {
    public:
        NotifyItemType type_;
        QString title_;
        QString body_;
        std::function<void()> cbk_;
    };

}

#endif //PX_NOTIFY_DEFS_H
