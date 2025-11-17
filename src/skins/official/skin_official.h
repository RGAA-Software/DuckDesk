//
// Created by RGAA on 13/11/2025.
//

#ifndef GAMMARAYPREMIUM_SKIN_OFFICIAL_H
#define GAMMARAYPREMIUM_SKIN_OFFICIAL_H

#include "skin/interface/skin_interface.h"

namespace tc
{
    class SkinOfficial : public SkinInterface {
    public:
        QString GetSkinName() override;

        // app name
        QString GetAppName();

        // version
        // eg: 1.3.5
        QString GetAppVersionName();

        // eg: Premium / Pro ...
        QString GetAppVersionMode();

        // colors
        int GetPrimaryColor();

        int GetSecondaryColor();

        int GetHeadTextColor();

        int GetSubHeadTextColor();

        int GetMainTextColor();

        int GetSecondaryTextColor();

        // icons
        QPixmap GetWindowIcon();

        QPixmap GetLargeIconTextLogo();

        QPixmap GetSquareIconLogo();
        
    };
}

extern "C" __declspec(dllexport) void* GetInstance();

#endif //GAMMARAYPREMIUM_SKIN_OFFICIAL_H
