//
// Created by RGAA on 2023/8/16.
//

#ifndef SAILFISH_SERVER_INFORMATIONCONTENT_H
#define SAILFISH_SERVER_INFORMATIONCONTENT_H
#include <QLabel>
#include <QWidget>
#include <memory>
#include <functional>
#include "px_cms_client/cms_stream.h"

namespace px
{

    class PxContext;
    class AppStreamList;

    using OnStartingStreamCallback = std::function<void(const px_cms::CmsStream&)>;

    class AddButton : public QLabel {
    public:

        explicit AddButton(QWidget* parent = nullptr);

        void SetOnClickCallback(std::function<void()>&& cbk) {
            click_cbk_ = std::move(cbk);
        }

        void paintEvent(QPaintEvent *) override;
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *ev) override;
        void mouseReleaseEvent(QMouseEvent *ev) override;

    private:
        std::function<void()> click_cbk_;

        bool enter_ = false;
        bool pressed_ = false;

    };

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - //

    class StreamContent : public QWidget {
    public:
        explicit StreamContent(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        ~StreamContent() override;
        void resizeEvent(QResizeEvent *event) override;

        void ShowEmptyTip();
        void HideEmptyTip();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        AppStreamList* stream_list_ = nullptr;
        AddButton* add_btn_ = nullptr;
        QLabel* empty_tip_ = nullptr;

    };

}

#endif //SAILFISH_SERVER_INFORMATIONCONTENT_H
