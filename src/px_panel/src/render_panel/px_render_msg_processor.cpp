//
// Created by RGAA on 26/07/2025.
//

#include "px_render_msg_processor.h"
#include "px_common_new/log.h"
#include "px_message.pb.h"
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"
#include "render_panel/clipboard/panel_clipboard_manager.h"

namespace px
{

    GrRenderMsgProcessor::GrRenderMsgProcessor(const std::shared_ptr<GrContext>& ctx) {
        context_ = ctx;
    }

    void GrRenderMsgProcessor::OnMessage(std::shared_ptr<px::Message> msg) const {
        // USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy
        (void)msg;
#if 0
        // clipboard
        if (const auto ctx = context_.lock()) {
            const auto app = ctx->GetApplication();
            if (!app) {
                return;
            }
            if (const auto clipboard_mgr = app->GetClipboardManager()) {
                clipboard_mgr->OnRemoteClipboardInfo(msg);
            }
        }
#endif
    }

}