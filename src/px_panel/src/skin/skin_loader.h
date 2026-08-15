//
// Created by RGAA on 13/11/2025.
//

#ifndef GAMMARAYPREMIUM_SKIN_LOADER_H
#define GAMMARAYPREMIUM_SKIN_LOADER_H

#include <string>

namespace tc
{

    class SkinInterface;

    class SkinLoader {
    public:
        static SkinInterface* LoadSkin(const std::string& skin_name_hint = "");
    };

}

#endif //GAMMARAYPREMIUM_SKIN_LOADER_H
