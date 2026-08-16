//
// Created by RGAA on 21/05/2025.
//

#ifndef PX_INIFINITE_LOADING_H
#define PX_INIFINITE_LOADING_H

#include <memory>
#include <QDialog>

namespace px_cms
{
    class CmsStream;
}

namespace px
{

    class PxContext;
    class Win10CircleLoadingWidget;
    class Win10HorizontalLoadingWidget;

    class InfiniteLoading : public QDialog {
    public:
        InfiniteLoading(const std::shared_ptr<PxContext>& ctx, const QString& msg);
        void resizeEvent(QResizeEvent *event) override;
        void paintEvent(QPaintEvent *event) override;
        void Close();

    private:
        std::shared_ptr<px_cms::CmsStream> stream_item_ = nullptr;
        Win10HorizontalLoadingWidget* h_loading_widget_ = nullptr;

    };

}

#endif //PX_START_STREAM_LOADING_H
