//
// Created by RGAA on 20/10/2025.
//

#ifndef GAMMARAYPREMIUM_SRV_INFO_PARSER_H
#define GAMMARAYPREMIUM_SRV_INFO_PARSER_H

#include <memory>
#include <string>

namespace tc
{

    class SpvrAccessInfo;

    class SpvrAccessInfoParser {
    public:
        static std::shared_ptr<SpvrAccessInfo> ParseInfo(const std::string& info);
    };

}

#endif //GAMMARAYPREMIUM_SRV_INFO_PARSER_H
