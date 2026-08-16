//
// Created by RGAA on 28/05/2025.
//

#ifndef PX_ST_SECURITY_VISITOR_H
#define PX_ST_SECURITY_VISITOR_H

#include "tab_base.h"
#include <memory>
#include <QListWidget>

namespace px
{

    class PageWidget;
    class PxApplication;
    class VisitRecord;
    class VisitRecordOperator;
    class StSecurityVisitorItemWidget;

    class StSecurityVisitor : public TabBase {
    public:
        StSecurityVisitor(const std::shared_ptr<PxApplication>& app, QWidget *parent);
        void OnTranslate() override;

    private:
        QListWidgetItem* AddItem(const std::shared_ptr<VisitRecord>& record);
        void LoadPage(int page);
        void RegisterActions(int index);
        void ProcessCopy(const std::shared_ptr<VisitRecord>& record);
        void ProcessCopyAsJson(const std::shared_ptr<VisitRecord>& record);
        void ProcessDelete(const std::shared_ptr<VisitRecord>& record);

    private:
        PageWidget* page_widget_ = nullptr;
        QListWidget* list_widget_ = nullptr;
        std::shared_ptr<VisitRecordOperator> visit_op_ = nullptr;
        std::vector<std::shared_ptr<VisitRecord>> records_;
        StSecurityVisitorItemWidget* header_item_ = nullptr;
    };

}
#endif //PX_ST_SECURITY_VISITOR_H
