//
// Created by RGAA on 25/06/2025.
//

#ifndef GAMMARAY_NOTIFY_DEFS_H
#define GAMMARAY_NOTIFY_DEFS_H

#include <QString>
#include <functional>

namespace tc
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

#endif //GAMMARAY_NOTIFY_DEFS_H
