//
// Created by RGAA on 2024/4/12.
//

#include "px_resources.h"
#include "px_context.h"

#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QApplication>

#include "px_common_new/log.h"

namespace px
{

    PxResources::PxResources(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        res_folder_path_ = QApplication::applicationDirPath() +  "/resources";
        QDir res_dir(res_folder_path_);
        if (!res_dir.exists()) {
            if (!res_dir.mkdir(res_folder_path_)) {
                LOGE("create folder failed: {}", res_folder_path_.toStdString());
            }
        }
    }

    void PxResources::ExtractIconsIfNeeded() {
        const auto res_folder_path = res_folder_path_;
        context_->PostTask([res_folder_path] {
            for (int i = 1; i <= 30; i++) {
                QString target_path = res_folder_path + "/" + std::format("{}.png", i).c_str();
                if (QFile::exists(target_path)) {
                    continue;
                }
                auto png_name = std::format(":/icons/{}.png", i);
                QImage image;
                image.load(png_name.c_str());
                auto pixmap = QPixmap::fromImage(image);
                pixmap.save(target_path);
            }
        });
    }

}
