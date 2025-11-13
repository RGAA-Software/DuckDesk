//
// Created by RGAA on 13/11/2025.
//

#ifndef GAMMARAYPREMIUM_SKIN_OFFICIAL_H
#define GAMMARAYPREMIUM_SKIN_OFFICIAL_H

#include "render_panel/skin/skin_interface.h"

namespace tc
{
    class SkinOfficial : public SkinInterface {
    public:
        std::string GetSkinName() override;
    };
}

#endif //GAMMARAYPREMIUM_SKIN_OFFICIAL_H
