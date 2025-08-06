//
// Created by RGAA on 6/08/2025.
//

#ifndef GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
#define GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H

#include "panel_companion.h"

namespace tc
{

    class PanelCompanionImpl : public PanelCompanion {
    public:
        ~PanelCompanionImpl();
    };

}

extern "C" __declspec(dllexport) void* GetInstance();

#endif //GAMMARAYPREMIUM_PANEL_COMPANION_IMPL_H
