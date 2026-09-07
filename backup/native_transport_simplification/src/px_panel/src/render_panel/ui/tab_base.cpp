//
// Created by RGAA on 2024/4/9.
//

#include "tab_base.h"
#include <QPointer>
#include "render_panel/px_settings.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "px_common/message_notifier.h"
#include "render_panel/px_statistics.h"
#include "render_panel/px_app_messages.h"

namespace px
{
    TabBase::TabBase(const std::shared_ptr<PxApplication>& app, QWidget* parent) : QWidget(parent) {
        app_ = app;
        context_ = app->GetContext();
        settings_ = PxSettings::Instance();
        msg_listener_ = context_->ObtainUIMessageListener();
        statistics_ = PxStatistics::Instance();

        QPointer<TabBase> self(this);
        msg_listener_->Listen<MsgLanguageChanged>([self](const MsgLanguageChanged&) {
            if (self) {
                self->OnTranslate();
            }
        });
    }

    TabBase::~TabBase() {
        if (msg_listener_) {
            msg_listener_->UnListenAll();
        }
    }

    void TabBase::OnTabShow() {

    }

    void TabBase::OnTabHide() {

    }

    void TabBase::OnTranslate() {

    }
}
