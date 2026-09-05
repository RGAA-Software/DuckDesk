//
// Created by RGAA on 20/02/2025.
//

#include "ct_main_progress.h"
#include <QPainter>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include "px_label.h"
#include "ct_settings.h"
#include "px_pushbutton.h"
#include "ct_app_message.h"
#include "no_margin_layout.h"
#include "px_common/log.h"
#include "ct_client_context.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_client_sdk/sdk_messages.h"

namespace px
{

    MainProgress::MainProgress(const std::shared_ptr<ThunderSdk>& sdk, const std::shared_ptr<ClientContext>& ctx, QWidget* parent) : QLabel(parent) {
        const QPointer<MainProgress> guarded_self(this);
        sdk_ = sdk;
        context_ = ctx;
        settings_ = Settings::Instance();
        auto root_layout = new NoMarginVLayout();
        root_layout->addStretch(1);
        // logo
        {
            auto layout = new NoMarginHLayout();
            auto logo = new QLabel(this);
            logo->setFixedSize(100, 100);
            logo->setStyleSheet(R"(border-image: url(:/resources/px_trans_icon_blue.png)})");
            layout->addStretch();
            layout->addWidget(logo);
            layout->addStretch();

            root_layout->addLayout(layout);
        }

        // title
        {
            auto layout = new NoMarginHLayout();
            auto title = new TcLabel(this);
            title->SetTextId("id_connecting_server");
            title->setStyleSheet(R"(font-size: 20px; font-weight: 500;)");
            layout->addStretch();
            layout->addWidget(title);
            layout->addStretch();
            root_layout->addSpacing(40);
            root_layout->addLayout(layout);
        }

        // progress bar
        {
            LOGI("For progressbar, network type: {}, total steps: {}", (int)settings_->network_type_, sdk_->GetProgressSteps());
            auto layout = new NoMarginHLayout();
            auto progress_bar = new QProgressBar();
            progress_bar_ = progress_bar;
            progress_bar->setFixedSize(400, 6);
            progress_bar->setMaximum(sdk_->GetProgressSteps());
            progress_bar->setValue(0);
            layout->addStretch();
            layout->addWidget(progress_bar);
            layout->addStretch();

            root_layout->addSpacing(20);
            root_layout->addLayout(layout);
        }

        // sub messages
        {
            auto layout = new NoMarginHLayout();
            auto lbl = new TcLabel(this);
            lbl_sub_message_ = lbl;
            lbl->SetTextId("id_start_connection");
            lbl->setStyleSheet(R"(font-size: 16px;;)");
            layout->addStretch();
            layout->addWidget(lbl);
            layout->addStretch();
            root_layout->addSpacing(30);
            root_layout->addLayout(layout);
        }

        // retry button and cancel button
        {
            auto layout = new NoMarginHLayout();

            //retry button
            {
                retry_btn_ = new TcPushButton(this);
                retry_btn_->setFixedSize(150, 30);
                retry_btn_->SetTextId("id_retry_conn");
                layout->addStretch();
                layout->addWidget(retry_btn_);
                root_layout->addSpacing(20);
                root_layout->addLayout(layout);
                retry_btn_->hide();
                connect(retry_btn_, &QPushButton::clicked, this, [guarded_self]() {
                    if (guarded_self) {
                        guarded_self->context_->SendAppMessage(MsgExitControlledEndExe{});
                    }
                });
            }

            //cancel button
            {
                auto lbl = new TcPushButton(this);
                lbl->setFixedSize(150, 30);
                lbl->SetTextId("id_cancel");
                layout->addSpacing(30);
                layout->addWidget(lbl);
                layout->addStretch();
                root_layout->addSpacing(30);
                root_layout->addLayout(layout);
                connect(lbl, &QPushButton::clicked, this, [guarded_self]() {
                    if (guarded_self) {
                        guarded_self->context_->SendAppMessage(MsgClientExitApp{});
                    }
                });
            }
        }

        root_layout->addStretch(5);
        setLayout(root_layout);

        QImage image;
        image.load(":/resources/image/ic_loading_bg.svg");
        bg_pixmap_ = QPixmap::fromImage(image);
        bg_pixmap_ = bg_pixmap_.scaled(640, 640);

        // listeners
        msg_listener_ = ctx->ObtainUIMessageListener();

        // begin to start
        msg_listener_->Listen<SdkMsgNetworkConnected>([guarded_self](const SdkMsgNetworkConnected&) {
            if (guarded_self) {
                guarded_self->context_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->lbl_sub_message_->SetTextId("id_start_connection");
                    }
                });
            }
        });

        // reconnection
        msg_listener_->Listen<SdkMsgReconnect>([guarded_self](const SdkMsgReconnect&) {
            if (guarded_self) {
                guarded_self->context_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->lbl_sub_message_->SetTextId("id_start_connection");
                    }
                });
            }
        });

        // configuration from remote device
        msg_listener_->Listen<SdkMsgFirstConfigInfoCallback>(
            [guarded_self](const SdkMsgFirstConfigInfoCallback&) {
            if (guarded_self) {
                guarded_self->context_->PostUITask([guarded_self]() {
                    if (!guarded_self) {
                        return;
                    }
                    guarded_self->lbl_sub_message_->SetTextId("id_has_connection");
                    QTimer::singleShot(3000, [guarded_self]() {
                        if (guarded_self) {
                            guarded_self->retry_btn_->show();
                        }
                    });
                });
            }
        });

        // first video frame
        msg_listener_->Listen<SdkMsgFirstVideoFrameDecoded>(
            [guarded_self](const SdkMsgFirstVideoFrameDecoded&) {
            if (guarded_self) {
                guarded_self->context_->PostUITask([guarded_self]() {
                    if (guarded_self) {
                        guarded_self->lbl_sub_message_->SetTextId("id_has_video_frame");
                    }
                });
            }
        });
    }

    void MainProgress::ResetProgress() {
        progress_steps_ = 0;
        if (retry_btn_) {
            retry_btn_->hide();
        }
        const QPointer<MainProgress> guarded_self(this);
        context_->PostUITask([guarded_self]() {
            if (!guarded_self || !guarded_self->parentWidget()) {
                return;
            }
            guarded_self->resize(guarded_self->parentWidget()->size());
            guarded_self->show();
            guarded_self->progress_bar_->setValue(guarded_self->progress_steps_);
        });
    }

    void MainProgress::StepForward() {
        progress_steps_++;
        const QPointer<MainProgress> guarded_self(this);
        context_->PostUITask([guarded_self]() {
            if (guarded_self) {
                guarded_self->progress_bar_->setValue(guarded_self->progress_steps_);
            }
        });
    }

    void MainProgress::CompleteProgress() {
        progress_steps_ = progress_bar_->maximum();
        const QPointer<MainProgress> guarded_self(this);
        context_->PostUITask([guarded_self]() {
            if (!guarded_self) {
                return;
            }
            guarded_self->progress_bar_->setValue(guarded_self->progress_steps_);
            QTimer::singleShot(150, [guarded_self]() {
                if (guarded_self) {
                    guarded_self->hide();
                }
            });

        });
    }

    int MainProgress::GetCurrentProgress() {
        return progress_steps_;
    }

    void MainProgress::paintEvent(QPaintEvent *event) {
        QPainter painter(this);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(0xffffff));
        painter.drawRect(this->rect());

        if (!bg_pixmap_.isNull()) {
            painter.drawPixmap((this->width() - bg_pixmap_.width())/2, (this->height() - bg_pixmap_.height())/2, bg_pixmap_);
        }
        QLabel::paintEvent(event);
    }

}
