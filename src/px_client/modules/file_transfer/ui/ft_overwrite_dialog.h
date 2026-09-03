//
// ft 覆盖确认弹框 — rustdesk override_file_confirm 语义(file_model.dart:154)
// 显示文件名/大小/mtime;按钮:跳过 / 覆盖 / 续传(有续传凭证时);
// "应用到全部冲突"勾选 -> SetOverwriteStrategy。
//

#ifndef PX_CLIENT_FT_OVERWRITE_DIALOG_H
#define PX_CLIENT_FT_OVERWRITE_DIALOG_H

#include <QDialog>
#include <QString>
#include <cstdint>

namespace px
{

    struct FtOverwriteDecision {
        int choice = 0;        // 0=skip 1=overwrite 2=resume
        uint64_t offset = 0;   // resume 时的字节偏移
        bool apply_to_all = false;
    };

    class FtOverwriteDialog : public QDialog {
        Q_OBJECT
    public:
        // path: 发生冲突的本端文件路径(下载方向为本地落点,上传方向为本地源文件)
        FtOverwriteDialog(const QString& path, bool is_upload, bool is_identical,
                          QWidget* parent = nullptr);

        FtOverwriteDecision Decision() const { return decision_; }

    private:
        FtOverwriteDecision decision_;
    };

}

#endif //PX_CLIENT_FT_OVERWRITE_DIALOG_H
