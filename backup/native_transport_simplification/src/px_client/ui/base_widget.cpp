//
// Created by RGAA on 3/07/2024.
//

#include "base_widget.h"
#include "px_client/ct_client_context.h"
#include "ct_app_message.h"
#include <QPointer>
namespace px
{

    BaseWidget::BaseWidget(const std::shared_ptr<ClientContext>& ctx, QWidget* parent) : QWidget(parent), context_(ctx) {
        CreateMsgListener();
    }

    void BaseWidget::CreateMsgListener() {
        msg_listener_ = context_->ObtainUIMessageListener();
        const QPointer<BaseWidget> guarded_self(this);
        msg_listener_->Listen<MsgClientFloatControllerPanelUpdate>(
            [guarded_self](const MsgClientFloatControllerPanelUpdate& msg) {
                if (guarded_self) {
                    guarded_self->UpdateStatus(msg);
                }
            });
    }

    void BaseWidget::SetOnClickListener(OnClickListener&& l) {
        click_listener_ = l;
    }

    void BaseWidget::Hide() {
        this->hide();
    }

    void BaseWidget::Show() {
        this->show();
    }

}
