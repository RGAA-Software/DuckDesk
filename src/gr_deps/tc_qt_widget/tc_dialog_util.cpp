//
// Created by RGAA on 19/11/2025.
//

#include "tc_dialog_util.h"
#include <QFileDialog>

namespace tc
{

    QString TcDialogUtil::SelectDirectory(const QString& title, const QString& default_path, QWidget* parent) {
        QString dir = QFileDialog::getExistingDirectory(
                parent,
                title,
                default_path,   // 默认打开的目录
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

        return dir;
    }

    QString TcDialogUtil::SelectImage(const QString& title, const QString& default_path, QWidget* parent) {
        // 设置图片文件过滤器
        QString filter = "Image Files (*.png *.jpg *.jpeg)";

        // 弹出文件选择对话框
        QString fileName = QFileDialog::getOpenFileName(
                parent,
                title,
                default_path,
                filter
        );

        return fileName;
    }

}