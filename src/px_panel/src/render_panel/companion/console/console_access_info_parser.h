//
// Created by RGAA on 20/10/2025.
//

#ifndef GAMMARAYPREMIUM_SRV_INFO_PARSER_H
#define GAMMARAYPREMIUM_SRV_INFO_PARSER_H

#include <memory>
#include <string>

namespace px
{

    class ConsoleAccessInfo;

    class ConsoleAccessInfoParser {
    public:
        static std::shared_ptr<ConsoleAccessInfo> ParseInfo(const std::string& info);
    };

}

#endif //GAMMARAYPREMIUM_SRV_INFO_PARSER_H
