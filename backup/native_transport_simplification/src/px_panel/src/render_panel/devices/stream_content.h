//
// Created by RGAA on 2023/8/16.
//

#ifndef SAILFISH_SERVER_INFORMATIONCONTENT_H
#define SAILFISH_SERVER_INFORMATIONCONTENT_H
#include <QLabel>
#include <QPointer>
#include <QWidget>
#include <memory>
#include <functional>
#include "px_console_client/console_stream.h"

namespace px
{

    class PxContext;
    class AppStreamList;

    using OnStartingStreamCallback = std::function<void(const px_console::ConsoleStream&)>;

    class AddButton : public QLabel {
    public:

        // QWidget parent is a transient Qt ownership boundary and is never retained.
        explicit AddButton(QWidget* parent = nullptr);

        void SetOnClickCallback(std::function<void()>&& cbk) {
            click_cbk_ = std::move(cbk);
        }

        void paintEvent(QPaintEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.
        void enterEvent(QEnterEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.
        void leaveEvent(QEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.
        void mousePressEvent(QMouseEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.
        void mouseReleaseEvent(QMouseEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.

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
        void resizeEvent(QResizeEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.

        void ShowEmptyTip();
        void HideEmptyTip();

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        QPointer<AppStreamList> stream_list_;
        QPointer<AddButton> add_btn_;
        QPointer<QLabel> empty_tip_;

    };

}

#endif //SAILFISH_SERVER_INFORMATIONCONTENT_H
