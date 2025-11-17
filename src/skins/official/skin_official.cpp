//
// Created by RGAA on 13/11/2025.
//

#include "skin_official.h"
#include "version_config.h"

void* GetInstance() {
    static tc::SkinOfficial impl;
    return (void*)&impl;
}

namespace tc
{

    QString SkinOfficial::GetSkinName() {
        return "Official";
    }

    // app name
    QString SkinOfficial::GetAppName() {
        return "GoDesk";
    }

    // version
    // eg: 1.3.5
    QString SkinOfficial::GetAppVersionName() {
        return PROJECT_VERSION;
    }

    // eg: Premium / Pro ...
    QString SkinOfficial::GetAppVersionMode() {
        return "Premium";
    }

    // colors
    int SkinOfficial::GetPrimaryColor() {
        return 0;
    }

    int SkinOfficial::GetSecondaryColor() {
        return 0;
    }

    int SkinOfficial::GetHeadTextColor() {
        return 0;
    }

    int SkinOfficial::GetSubHeadTextColor() {
        return 0;
    }

    int SkinOfficial::GetMainTextColor() {
        return 0;
    }

    int SkinOfficial::GetSecondaryTextColor() {
        return 0;
    }

    // icons
    QPixmap SkinOfficial::GetWindowIcon() {
        QPixmap p;
        return p;
    }

    QPixmap SkinOfficial::GetLargeIconTextLogo() {
        QPixmap p;
        p.load(":/skin/resources/tc_logo_text_trans_bg.png");
        return p;
    }

    QPixmap SkinOfficial::GetSquareLogo() {
        QPixmap p;
        p.load(":/skin/resources/tc_icon.png");
        return p;
    }

    QPixmap SkinOfficial::GetSquarePrimaryColorLogoTransBg() {
        QPixmap p;
        return p;
    }

    QPixmap SkinOfficial::GetSquareWhiteLogoTransBg() {
        QPixmap p;
        return p;
    }

}