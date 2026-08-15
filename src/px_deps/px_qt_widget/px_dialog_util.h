//
// Created by RGAA on 19/11/2025.
//

#ifndef GAMMARAYPREMIUM_TC_DIALOG_UTIL_H
#define GAMMARAYPREMIUM_TC_DIALOG_UTIL_H

#include <QString>
#include <QWidget>

namespace tc
{

    class TcDialogUtil {
    public:
        static QString SelectDirectory(const QString& title, const QString& default_path, QWidget* parent = nullptr);
        static QString SelectImage(const QString& title, const QString& default_path, QWidget* parent = nullptr);
    };

}

#endif //GAMMARAYPREMIUM_TC_DIALOG_UTIL_H
