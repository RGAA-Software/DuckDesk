//
// Created by RGAA on 28/05/2025.
//

#ifndef PX_ST_SECURITY_VISITOR_H
#define PX_ST_SECURITY_VISITOR_H

#include "tab_base.h"
#include <memory>
#include <QListWidget>
#include <QPointer>

namespace px
{

    class PageWidget;
    class PxApplication;
    class VisitRecord;
    class VisitRecordOperator;
    class StSecurityVisitorItemWidget;
    class LatestAsyncGeneration;

    class StSecurityVisitor : public TabBase {
    public:
        StSecurityVisitor(
            const std::shared_ptr<PxApplication>& app,
            QWidget* parent); // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI.
        ~StSecurityVisitor() override;
        void OnTranslate() override;

    private:
        void AddItem(const std::shared_ptr<VisitRecord>& record);
        void LoadPage(int page);
        void RegisterActions(int index);
        void ProcessCopy(const std::shared_ptr<VisitRecord>& record);
        void ProcessCopyAsJson(const std::shared_ptr<VisitRecord>& record);
        void ProcessDelete(const std::shared_ptr<VisitRecord>& record);

    private:
        QPointer<PageWidget> page_widget_;
        QPointer<QListWidget> list_widget_;
        std::shared_ptr<VisitRecordOperator> visit_op_ = nullptr;
        std::vector<std::shared_ptr<VisitRecord>> records_;
        QPointer<StSecurityVisitorItemWidget> header_item_;
        std::shared_ptr<LatestAsyncGeneration> load_generation_ = nullptr;
    };

}
#endif //PX_ST_SECURITY_VISITOR_H
