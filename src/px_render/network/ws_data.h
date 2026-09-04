//
// Created by RGAA on 27/05/2025.
//

#ifndef PX_WS_DATA_H
#define PX_WS_DATA_H

#include <memory>

namespace px
{
    class WsPlugin;

    class WsData {
    public:
        // Weak observer: routers never extend the built-in WS module lifetime.
        std::weak_ptr<WsPlugin> plugin_;
    };
    using WsDataPtr = std::shared_ptr<WsData>;
}

#endif //PX_WS_DATA_H
