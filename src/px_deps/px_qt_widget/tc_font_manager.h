//
// Created by RGAA on 22/03/2025.
//

#ifndef GAMMARAY_TC_FONT_MANAGER_H
#define GAMMARAY_TC_FONT_MANAGER_H

#include <QString>
#include <QFont>

#define tcFontMgr() TcFontManager::Instance()

namespace tc
{

    class TcFontManager {
    public:
        static TcFontManager* Instance() {
            static TcFontManager fm;
            return &fm;
        }

        void InitFont(const QString& font_path);

    public:
        QString font_name_;
        QFont font_22_;
    };

}

#endif //GAMMARAY_TC_FONT_MANAGER_H
