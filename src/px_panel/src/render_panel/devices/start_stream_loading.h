//
// Created by RGAA on 21/05/2025.
//

#ifndef PX_START_STREAM_LOADING_H
#define PX_START_STREAM_LOADING_H

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

    class StartStreamLoading : public QDialog {
    public:
        StartStreamLoading(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<px_cms::CmsStream>& item, const std::string& network_type);
        void resizeEvent(QResizeEvent *event) override;
        void paintEvent(QPaintEvent *event) override;

    private:
        std::shared_ptr<px_cms::CmsStream> stream_item_ = nullptr;
        Win10HorizontalLoadingWidget* h_loading_widget_ = nullptr;

    };

}

#endif //PX_START_STREAM_LOADING_H
