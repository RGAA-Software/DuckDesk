//
// ft 三栏文件管理窗口 — 本地 | 远程 | 传输队列(参照 rustdesk file_manager_page.dart)
//

#ifndef PX_CLIENT_FT_WINDOW_H
#define PX_CLIENT_FT_WINDOW_H

#include <QWidget>
#include <QHash>
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
        // FT 通道首次连接/重连成功后刷新远端目录，不改变窗口前后台状态。
        void OnTransportConnected();
        // 远程栏标题显示对端标识(设备 ID 或名称)
        void SetRemoteDeviceName(const QString& name);

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

        // 作业方向(job id -> 是否下载),完成后刷新接收侧列表用
        QHash<int, bool> job_download_;
    };

}

#endif //PX_CLIENT_FT_WINDOW_H
