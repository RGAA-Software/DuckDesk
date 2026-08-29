//
// ft 主控端插件 core — px_ft_engine 的薄适配层(rustdesk 协议迁移阶段 3)
// 模型与 render 壳(src/px_render/plugins/ft)一致:
//   UI/网络入口只向 FtAsyncSession 投递命令，引擎状态与发送泵全部在 state strand。
// UI 通信经 Qt 信号(Session 线程 emit,自动 QueuedConnection 到 UI 线程)。
//

#ifndef PX_CLIENT_FT_CORE_H
#define PX_CLIENT_FT_CORE_H

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

#include "px_common_new/file_transfer_send_result.h"

namespace px::ft
{
    class FtAsyncSession;
    class FtEngine;
}

namespace px
{
    class Message;
    class FileResponse;
    class FileDirectory;
    class ReadEmptyDirsResponse;

    // 目录项(本地/远程面板共用)
    class FtEntryInfo {
    public:
        QString name_;
        bool is_dir_ = false;
        bool is_drive_ = false;
        uint64_t size_ = 0;
        int64_t modified_time_ = 0; // 秒
        // 绝对路径(仅根视图常用文件夹项携带):name 是本地化显示名,导航用 abs_path_
        QString abs_path_;
    };

    using FtEntryList = QList<FtEntryInfo>;

    // 作业进度快照(对齐引擎 TransferJobStatus,Qt 元类型化)
    class FtJobStatusInfo {
    public:
        int32_t id_ = 0;
        int32_t file_num_ = 0;
        int32_t file_count_ = 0;
        uint64_t total_size_ = 0;
        uint64_t finished_size_ = 0;
        double speed_ = 0.0;      // 字节/秒
        bool is_download_ = false; // true: 远端 -> 本地
        bool done_ = false;
        bool cancel_ = false;
        QString error_;
    };

    class FtCore : public QObject {
        Q_OBJECT
    public:
        using SendCallback =
            std::function<FileTransferSendResult(const Message&)>;

        explicit FtCore(SendCallback send_callback);
        ~FtCore() override;

        void Start();
        void Stop();

        // ---------------- 网络入口(分发线程调用,只入队) ----------------
        void EnqueueMessage(const std::shared_ptr<Message>& msg);

        // ---------------- UI 主动操作(UI 线程调用,只入队) ----------------
        void ReadDir(const QString& path);
        void CreateDir(const QString& path);
        void RemoveEntry(const QString& path, bool is_dir);
        void RenameEntry(const QString& path, const QString& new_name);
        // 上传:本地文件/目录 -> 远程目录(逐个 item 建作业)
        void StartUpload(const QStringList& local_paths, const QString& remote_dir);
        // 下载:远程文件/目录 -> 本地目录
        void StartDownload(const QStringList& remote_paths, const QString& local_dir);
        void CancelJob(int32_t id);
        // 覆盖确认回喂:choice 0=skip 1=overwrite 2=resume(offset 续传)
        void ConfirmOverwrite(int32_t job_id, int32_t file_num, int choice, uint64_t offset, bool apply_to_all);
        void SetRateLimitBytesPerSec(uint64_t bps);

        bool HasJobs() const;

    signals:
        // 远程目录列表响应(read_dir 回包,含 "/" 盘符列表)
        void SigRemoteDir(const QString& path, const px::FtEntryList& entries);
        // 作业新增(UI 建行)
        void SigJobAdded(int id, const QString& name, bool is_download);
        // 每秒进度
        void SigJobProgress(const px::FtJobStatusInfo& st);
        // 作业终结(error_or_empty 为空=成功,"cancel"=取消)
        void SigJobDone(int id, const QString& error_or_empty);
        // 覆盖确认请求(需弹框)
        void SigOverwriteConfirm(int job_id, int file_num, const QString& path, bool is_upload, bool is_identical);
        // 目录操作回执(op id < 0;error 为空=成功)
        void SigDirOpDone(int op_id, const QString& error_or_empty);

    private:
        void ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                            const std::shared_ptr<Message>& msg);
        void ProcessResponse(const px::FileResponse& resp);

        // 上传目录前:本地展开空目录并批量 CreateDir(file_model.dart:570 语义)
        void CreateRemoteEmptyDirs(const std::shared_ptr<px::ft::FtEngine>& engine,
                                   int32_t job_id,
                                   const QString& local_path,
                                   const QString& remote_to);
        // 下载目录后:远端空目录在本地落地
        void CreateLocalEmptyDirs(const px::ReadEmptyDirsResponse& resp, const QString& local_dir);

        static FtEntryList ConvertEntries(const px::FileDirectory& dir);

    private:
        SendCallback send_callback_;
        std::atomic<std::shared_ptr<px::ft::FtAsyncSession>> session_;
        std::atomic_bool accepting_ = false;
        // 目录操作 id(负值,避开引擎作业 id 空间)
        int32_t next_op_id_ = -1;
        // 下载中的空目录查询:远端路径 -> 本地落点
        std::unordered_map<std::string, QString> pending_empty_dirs_;
    };

}

Q_DECLARE_METATYPE(px::FtEntryInfo)
Q_DECLARE_METATYPE(px::FtEntryList)
Q_DECLARE_METATYPE(px::FtJobStatusInfo)

#endif //PX_CLIENT_FT_CORE_H
