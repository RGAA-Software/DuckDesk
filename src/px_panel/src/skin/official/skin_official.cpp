//
// Created by RGAA on 13/11/2025.
//

#include "skin_official.h"
#include "version_config.h"

void* GetInstance() {
    static px::SkinOfficial impl;
    return (void*)&impl;
}

namespace px
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
        static QPixmap large_icon_text_logo;
        if (large_icon_text_logo.isNull()) {
            large_icon_text_logo.load(":/skin/resources/px_text_logo.png");
        }
        return large_icon_text_logo;
    }

    QPixmap SkinOfficial::GetSquareLogo() {
        static QPixmap square_logo;
        if (square_logo.isNull()) {
            square_logo.load(":/skin/resources/px_icon.png");
        }
        return square_logo;
    }

    bool SkinOfficial::IsGameEnabled() {
        return true;
    }

    bool SkinOfficial::IsCoPhoneEnabled() {
        return false;
    }

}