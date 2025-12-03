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
        static QPixmap large_icon_text_logo;
        if (large_icon_text_logo.isNull()) {
            large_icon_text_logo.load(":/skin/resources/tc_logo_text_trans_bg.png");
        }
        return large_icon_text_logo;
    }

    QPixmap SkinOfficial::GetSquareLogo() {
        static QPixmap square_logo;
        if (square_logo.isNull()) {
            square_logo.load(":/skin/resources/tc_icon.png");
        }
        return square_logo;
    }

    QPixmap SkinOfficial::GetSquarePrimaryColorLogoTransBg() {
        static QPixmap primary_color_logo_trans_bg;
        if (primary_color_logo_trans_bg.isNull()) {
            primary_color_logo_trans_bg.load(":/skin/resources/tc_trans_icon_blue.png");
        }
        return primary_color_logo_trans_bg;
    }

    QPixmap SkinOfficial::GetSquareWhiteLogoTransBg() {
        static QPixmap white_logo_trans_bg;
        if (white_logo_trans_bg.isNull()) {
            white_logo_trans_bg.load(":/skin/resources/tc_trans_icon_white.png");
        }
        return white_logo_trans_bg;
    }

    bool SkinOfficial::IsGameEnabled() {
        return true;
    }

    bool SkinOfficial::IsCoPhoneEnabled() {
        return false;
    }

}