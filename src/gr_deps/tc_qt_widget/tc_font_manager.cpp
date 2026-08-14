//
// Created by RGAA on 22/03/2025.
//

#include "tc_font_manager.h"
#include "tc_common_new/log.h"

namespace tc
{

    void TcFontManager::InitFont(const QString& /*font_path*/) {
        font_name_ = "Microsoft YaHei";
        LOGI("Font name: {}", font_name_.toStdString());

        font_22_ = QFont(font_name_);
        font_22_.setStyleStrategy(QFont::PreferAntialias);
        //font_22_.setPointSize(22);
        font_22_.setPixelSize(22);
    }

}