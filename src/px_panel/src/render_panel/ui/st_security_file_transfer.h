//
// Created by RGAA on 28/05/2025.
//

#ifndef PX_ST_SECURITY_FILE_TRANSFER_H
#define PX_ST_SECURITY_FILE_TRANSFER_H

#include "tab_base.h"
#include <memory>
#include <vector>
#include <QListWidget>
#include <QPointer>

namespace px
{

    class PageWidget;
    class PxApplication;
    class FileTransferRecord;
    class FileTransferRecordOperator;
    class LatestAsyncGeneration;

    class StSecurityFileTransfer : public TabBase {
    public:
        StSecurityFileTransfer(
            const std::shared_ptr<PxApplication>& app,
            QWidget* parent); // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI.
        ~StSecurityFileTransfer() override;

    private:
        void AddItem(const std::shared_ptr<FileTransferRecord>& item_info);
        void LoadPage(int page);
        void RegisterActions(int index);
        void ProcessCopy(const std::shared_ptr<FileTransferRecord>& record);
        void ProcessCopyAsJson(const std::shared_ptr<FileTransferRecord>& record);
        void ProcessDelete(const std::shared_ptr<FileTransferRecord>& record);

    private:
        QPointer<PageWidget> page_widget_;
        QPointer<QListWidget> list_widget_;
        std::shared_ptr<FileTransferRecordOperator> ft_record_op_ = nullptr;
        std::vector<std::shared_ptr<FileTransferRecord>> records_;
        std::shared_ptr<LatestAsyncGeneration> load_generation_ = nullptr;
    };

}
#endif //PX_ST_SECURITY_VISITOR_H
