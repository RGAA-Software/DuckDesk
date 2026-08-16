//
// ft 三栏文件管理窗口 — 本地 | 远程 | 传输队列(参照 rustdesk file_manager_page.dart)
//

#ifndef PX_CLIENT_FT_WINDOW_H
#define PX_CLIENT_FT_WINDOW_H

#include <QWidget>
#include <deque>

#include "../ft_core.h"

class QSplitter;

namespace px
{

    class FtFilePanel;
    class FtTransferQueue;

    class FtWindow : public QWidget {
        Q_OBJECT
    public:
        FtWindow(FtCore* core, QWidget* parent = nullptr);

        // 插件 ShowRootWidget 时调用:首显初始化两侧目录
        void OnShow();

    private:
        void ShowNextOverwriteConfirm();

    private:
        FtCore* core_ = nullptr;
        FtFilePanel* local_panel_ = nullptr;
        FtFilePanel* remote_panel_ = nullptr;
        FtTransferQueue* queue_ = nullptr;
        bool first_show_ = true;

        // 覆盖确认串行弹框队列
        struct PendingConfirm {
            int32_t job_id;
            int32_t file_num;
            QString path;
            bool is_upload;
            bool is_identical;
        };
        std::deque<PendingConfirm> pending_confirms_;
        bool confirm_showing_ = false;
    };

}

#endif //PX_CLIENT_FT_WINDOW_H
