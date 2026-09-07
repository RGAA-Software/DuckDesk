//
// Created by RGAA on 22/03/2025.
//

#include "tab_profile.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "no_margin_layout.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "px_common/log.h"
#include "px_qt_widget/clickable_widget.h"
#include "render_panel/px_context.h"
#ifdef WIN32
#include <Windows.h>
#include <shellapi.h>
#endif
#include <QLabel>
#include <QListWidget>

namespace px
{

    TabProfile::TabProfile(
        const std::shared_ptr<PxApplication>& app,
        QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent API
        : TabBase(app, parent) {
#ifdef WIN32
        auto hwnd = HWND(winId());
        ::DragAcceptFiles(hwnd, TRUE);
        ::ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
        ::ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        ::ChangeWindowMessageFilterEx(hwnd, 0x0049, MSGFLT_ALLOW, nullptr);
        setAcceptDrops(true);
#endif

        stacked_widget_ = new QStackedWidget(this); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it

        root_layout_ = new QHBoxLayout(this); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        root_layout_->setSpacing(0);
        root_layout_->setContentsMargins(0, 0, 0, 0);
        AddLeftProfileInfo();
        AddRightDetailInfo();
    }

    void TabProfile::OnTabShow() {

    }

    void TabProfile::OnTabHide() {

    }

    void TabProfile::dragEnterEvent(
        QDragEnterEvent* event) { // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI
        event->accept();
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
        LOGI("DragEventEnter....");
    }

    void TabProfile::dragMoveEvent(
        QDragMoveEvent* event) { // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI
        event->accept();
        LOGI("DragEventMove....");
    }

    void TabProfile::dropEvent(
        QDropEvent* event) { // NOLINT(gammaray-raw-pointer-boundary) Qt event ABI
        QList<QUrl> urls = event->mimeData()->urls();
        LOGI("DragEventDrop....{}", urls.size());
        if (urls.isEmpty()) {
            return;
        }
        std::vector<QString> files;
        for (const auto& url : urls) {
            files.push_back(url.toLocalFile());
            LOGI("Drop files: {}", url.toLocalFile().toStdString());
        }
        //if (file_transfer_) {
        //    file_transfer_->SendFiles(files);
        //}
    }

    void TabProfile::AddLeftProfileInfo() {
        auto widget = new QWidget(this);
        auto root_layout = new NoMarginVLayout();
        widget->setLayout(root_layout);
        widget->setFixedWidth(360);
        //widget->setStyleSheet("background-color: #eeeeee;");

        root_layout->addSpacing(20);

        // title
        {
            auto lbl = new QLabel(this);
            lbl->setText("Personal Information");
            lbl->setStyleSheet("font-size: 16px; font-weight: 700; color:#555555;");
            root_layout->addWidget(lbl);
        }

        root_layout->addSpacing(20);

        {
            auto item_layout = new NoMarginHLayout();

            // widget
            {
                auto icon = new QLabel(this);
                icon->setFixedSize(64, 64);
                QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
                icon->setStyleSheet(style.arg(":/resources/image/ic_empty_avatar.svg"));
                item_layout->addWidget(icon);
            }

            // info
            {
                auto account_layout = new NoMarginVLayout();

                // name
                auto lbl_account = new QLabel(this);
                lbl_account->setText("1880000dddd");
                lbl_account->setStyleSheet("font-size: 15px; font-weight: 700; color:#555555;");
                account_layout->addSpacing(10);
                account_layout->addWidget(lbl_account);

                account_layout->addSpacing(10);

                auto lbl_account_type = new QLabel(this);
                lbl_account_type->setFixedSize(85, 20);
                lbl_account_type->setAlignment(Qt::AlignCenter);
                lbl_account_type->setStyleSheet("font-size: 12px; color: #ffffff; background-color: #2979ff;");
                lbl_account_type->setText("Freemium");
                account_layout->addWidget(lbl_account_type);
                account_layout->addSpacing(20);

                item_layout->addSpacing(20);
                item_layout->addLayout(account_layout);
                item_layout->addStretch();
            }

            root_layout->addLayout(item_layout);
        }

        root_layout->addStretch(1000);
        root_layout_->addWidget(widget);
    }

    void TabProfile::AddRightDetailInfo() {
        auto widget = new QWidget(this);
        auto root_layout = new NoMarginVLayout();
        widget->setLayout(root_layout);
        widget->setFixedWidth(800);

        root_layout->addSpacing(20);

        // device details
        {
            auto lbl = new QLabel(this);
            lbl->setText("Device Details");
            lbl->setStyleSheet("font-size: 16px; font-weight: 700; color:#555555; padding-left: 6px;");
            root_layout->addWidget(lbl);
        }

        //
        {
            auto layout = new NoMarginHLayout();
            layout->addSpacing(6);
            auto bg_size = QSize(238, 90);
            auto icon_size = QSize(35, 35);
            // served
            {
                auto bg = new ClickableWidget(this);
                bg->setFixedSize(bg_size);
                bg->SetGradientColor(0xa1c4fd, 0xc2e9fb);
                bg->SetRadius(10);
                layout->addWidget(bg);

                auto item_layout = new NoMarginHLayout();
                item_layout->addSpacing(12);

                // info
                {
                    auto info_layout = new NoMarginVLayout();
                    info_layout->addSpacing(5);
                    // title
                    auto title = new TcLabel();
                    title->setText("Served Duration");
                    title->setStyleSheet("font-weight: 500; font-size: 14px; color: #ffffff;");
                    info_layout->addSpacing(8);
                    info_layout->addWidget(title);

                    // time
                    auto time = new TcLabel(this);
                    time->setText("1:09:54");
                    time->setStyleSheet("font-weight: 700; font-size: 27px; color: #ffffff;");
                    info_layout->addWidget(time);
                    info_layout->addSpacing(12);
                    info_layout->addStretch();

                    item_layout->addLayout(info_layout);
                }

                // icon
                {
                    auto icon = new QLabel(this);
                    icon->setFixedSize(icon_size);
                    QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
                    icon->setStyleSheet(style.arg(":/resources/image/ic_served.svg"));
                    item_layout->addSpacing(10);
                    item_layout->addWidget(icon);
                }

                item_layout->addSpacing(12);
                bg->setLayout(item_layout);
            }

            // controlled
            {
                auto bg = new ClickableWidget(this);
                bg->setFixedSize(bg_size);
                bg->SetGradientColor(0xa1c4fd, 0xc2e9fb);
                bg->SetRadius(10);
                layout->addSpacing(30);
                layout->addWidget(bg);

                auto item_layout = new NoMarginHLayout();
                item_layout->addSpacing(12);

                // info
                {
                    auto info_layout = new NoMarginVLayout();
                    info_layout->addSpacing(5);
                    // title
                    auto title = new TcLabel();
                    title->setText("Controlled Duration");
                    title->setStyleSheet("font-weight: 500; font-size: 14px; color: #ffffff;");
                    info_layout->addSpacing(8);
                    info_layout->addWidget(title);

                    // time
                    auto time = new TcLabel(this);
                    time->setText("2 Hours");
                    time->setStyleSheet("font-weight: 700; font-size: 27px; color: #ffffff;");
                    info_layout->addWidget(time);
                    info_layout->addSpacing(12);
                    info_layout->addStretch();

                    item_layout->addLayout(info_layout);
                }

                // icon
                {
                    auto icon = new QLabel(this);
                    icon->setFixedSize(icon_size);
                    QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
                    icon->setStyleSheet(style.arg(":/resources/image/ic_controller_hand.svg"));
                    item_layout->addSpacing(10);
                    item_layout->addWidget(icon);
                }

                item_layout->addSpacing(12);
                bg->setLayout(item_layout);
            }

            // device restricts
            {
                auto bg = new ClickableWidget(this);
                bg->setFixedSize(bg_size);
                bg->SetGradientColor(0xa1c4fd, 0xc2e9fb);
                bg->SetRadius(10);
                layout->addSpacing(30);
                layout->addWidget(bg);

                auto item_layout = new NoMarginHLayout();
                item_layout->addSpacing(12);

                // info
                {
                    auto info_layout = new NoMarginVLayout();
                    info_layout->addSpacing(5);
                    // title
                    auto title = new TcLabel();
                    title->setText("Managed Devices");
                    title->setStyleSheet("font-weight: 500; font-size: 14px; color: #ffffff;");
                    info_layout->addSpacing(8);
                    info_layout->addWidget(title);

                    // time
                    auto time = new TcLabel(this);
                    time->setText("2/100");
                    time->setStyleSheet("font-weight: 700; font-size: 27px; color: #ffffff;");
                    info_layout->addWidget(time);
                    info_layout->addSpacing(12);
                    info_layout->addStretch();

                    item_layout->addLayout(info_layout);
                }

                // icon
                {
                    auto icon = new QLabel(this);
                    icon->setFixedSize(icon_size);
                    QString style = R"(background-image: url(%1);
                        background-repeat: no-repeat;
                        background-position: center;
                    )";
                    icon->setStyleSheet(style.arg(":/resources/image/ic_device_used.svg"));
                    item_layout->addSpacing(10);
                    item_layout->addWidget(icon);
                }

                item_layout->addSpacing(12);
                bg->setLayout(item_layout);

            }
            layout->addStretch();
            root_layout->addSpacing(15);
            root_layout->addLayout(layout);
        }

        root_layout->addSpacing(20);

        // my devices
        {
            auto lbl = new QLabel(this);
            lbl->setText("My Devices");
            lbl->setStyleSheet("font-size: 16px; font-weight: 700; color:#555555; padding-left: 6px;");
            root_layout->addWidget(lbl);
        }

        root_layout->addSpacing(5);

        // my device list
        {
            list_widget_ = new QListWidget(this);
            list_widget_->setFixedWidth(512);

            list_widget_->setMovement(QListView::Static);
            list_widget_->setViewMode(QListView::ListMode);
            list_widget_->setFlow(QListView::TopToBottom);
            list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            list_widget_->setResizeMode(QListWidget::Adjust);
            list_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
            list_widget_->setSpacing(2);
            list_widget_->setStyleSheet(R"(
                QListWidget {
                    background-color: #ffffff;
                    border: 0px solid #ffffff;
                }
                QListWidget::item {
                    color: #ffffff;
                    border: transparent;
                    border-bottom: 0px solid #ffffff;
                }

                QListWidget::item:hover {
                    background-color: none;
                }

                QListWidget::item:selected {
                    border-left: 0px solid #777777;
                    background-color: none;
                }
            )");

            // list content
            auto content_layout = new NoMarginHLayout();
            content_layout->addWidget(list_widget_);

            // right
            content_layout->addWidget(stacked_widget_);
            stacked_widget_->addWidget(AddEmptyWidget());
            stacked_widget_->addWidget(AddOnlineInfoWidget());
            stacked_widget_->addWidget(AddOfflineInfoWidget());

            stacked_widget_->setCurrentIndex(0);

            root_layout->addLayout(content_layout);
        }

        root_layout_->addWidget(widget);
    }

    QPointer<QWidget> TabProfile::AddEmptyWidget() {
        const QPointer<QWidget> w =
            new QWidget(this); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        w->setStyleSheet("background-color:#ffffff;");
        return w;
    }

    QPointer<QWidget> TabProfile::AddOnlineInfoWidget() {
        const QPointer<QWidget> w =
            new QWidget(this); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        w->setStyleSheet("background-color:#eeeeee;");
        return w;
    }

    QPointer<QWidget> TabProfile::AddOfflineInfoWidget() {
        const QPointer<QWidget> w =
            new QWidget(this); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it
        w->setStyleSheet("background-color:#cccccc;");
        return w;
    }

}
