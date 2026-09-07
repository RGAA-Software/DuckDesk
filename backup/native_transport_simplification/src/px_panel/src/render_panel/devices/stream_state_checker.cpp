//
// Created by RGAA on 24/05/2025.
//

#include "stream_state_checker.h"
#include "connection_policy.h"
#include "px_common/thread.h"
#include "px_common/message_notifier.h"
#include "px_common/http_client.h"
#include "px_common/log.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "px_console_client/console_stream.h"

namespace px
{

    StreamStateChecker::StreamStateChecker(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
    }

    StreamStateChecker::~StreamStateChecker() {
        Exit();
    }

    void StreamStateChecker::Start() {

    }

    void StreamStateChecker::Exit() {
        if (exiting_.exchange(true)) {
            return;
        }
        std::lock_guard<std::mutex> lock(callback_mtx_);
        on_checked_cbk_ = {};
    }

    void StreamStateChecker::UpdateCurrentStreamItems(const std::vector<std::shared_ptr<px_console::ConsoleStream>>& items) {
        const auto context = context_.lock();
        if (!context || exiting_) {
            return;
        }
        const auto weak_self = weak_from_this();
        context->PostTask([weak_self, items]() {
            const auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->CheckState(items);
        });
    }

    void StreamStateChecker::SetOnCheckedCallback(OnStreamStateCheckedCallback&& cbk) {
        std::lock_guard<std::mutex> lock(callback_mtx_);
        if (!exiting_) {
            on_checked_cbk_ = std::move(cbk);
        }
    }

    void StreamStateChecker::CheckState(const std::vector<std::shared_ptr<px_console::ConsoleStream>>& items) {
        for (auto& item : items) {
            // Console application cards are catalog resources, not addressable
            // devices before a ticket is issued. Their state comes from the
            // application-instance API and must not be overwritten by an
            // empty host/port ping or a device-online query.
            if (item->connect_type_ == connection_policy::kConsoleAppTicket) {
                item->direct_online_ = item->console_instance_state_ == "running";
                item->relay_online_ = false;
                item->console_online_ = true;
                continue;
            }
            if (item->connect_type_ == connection_policy::kConsoleDeviceTicket) {
                // The authenticated Console catalog refresh owns this state.
                // Do not use the legacy AppKey device/relay probes for a
                // ticket-managed resource.
                item->direct_online_ = false;
                item->relay_online_ = false;
                continue;
            }
            // host & port mode
            // /api/ping
            item->direct_online_ = false;
            auto client = HttpClient::Make(item->stream_host_, item->stream_port_, "/api/ping", 1000);
            auto res = client->Request();
            if (res.status == 200) {
                item->direct_online_ = true;
            }

            item->relay_online_ = false;
            item->console_online_ = false;
        }

        OnStreamStateCheckedCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mtx_);
            callback = on_checked_cbk_;
        }
        if (callback && !exiting_) {
            callback(items);
        }
    }

}
