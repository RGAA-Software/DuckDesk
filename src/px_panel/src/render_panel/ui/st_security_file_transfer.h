//
// Created by RGAA on 28/05/2025.
//

#ifndef PX_ST_SECURITY_FILE_TRANSFER_H
#define PX_ST_SECURITY_FILE_TRANSFER_H

#include "tab_base.h"
#include <memory>
#include <vector>
#include <QListWidget>

namespace px
{

    class PageWidget;
    class PxApplication;
    class FileTransferRecord;
    class FileTransferRecordOperator;

    class StSecurityFileTransfer : public TabBase {
    public:
        StSecurityFileTransfer(const std::shared_ptr<PxApplication>& app, QWidget *parent);

    private:
        QListWidgetItem* AddItem(const std::shared_ptr<FileTransferRecord>& item_info);
        void LoadPage(int page);
        void RegisterActions(int index);
        void ProcessCopy(const std::shared_ptr<FileTransferRecord>& record);
        void ProcessCopyAsJson(const std::shared_ptr<FileTransferRecord>& record);
        void ProcessDelete(const std::shared_ptr<FileTransferRecord>& record);

    private:
        PageWidget* page_widget_ = nullptr;
        QListWidget* list_widget_ = nullptr;
        std::shared_ptr<FileTransferRecordOperator> ft_record_op_ = nullptr;
        std::vector<std::shared_ptr<FileTransferRecord>> records_;
    };

}
#endif //PX_ST_SECURITY_VISITOR_H
