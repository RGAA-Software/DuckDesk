//
// Created by RGAA on 27/05/2025.
//

#ifndef PX_WS_DATA_H
#define PX_WS_DATA_H

namespace px
{
    class WsData {
    public:
        std::map<std::string, std::any> vars_;
    };
    using WsDataPtr = std::shared_ptr<WsData>;
}

#endif //PX_WS_DATA_H
