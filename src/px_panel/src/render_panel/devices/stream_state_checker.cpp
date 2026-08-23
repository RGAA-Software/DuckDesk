//
// Created by RGAA on 24/05/2025.
//

#include "stream_state_checker.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "px_console_client/console_stream.h"
#include "relay_message.pb.h"
#include "px_relay_client/relay_api.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "px_console_client/console_device_api.h"

namespace px
{

    StreamStateChecker::StreamStateChecker(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
        msg_listener_ = context_->ObtainMessageListener();
        settings_ = PxSettings::Instance();
    }

    void StreamStateChecker::Start() {

    }

    void StreamStateChecker::Exit() {

    }

    void StreamStateChecker::UpdateCurrentStreamItems(const std::vector<std::shared_ptr<px_console::ConsoleStream>>& items) {
        const auto weak_self = weak_from_this();
        context_->PostTask([weak_self, items]() {
            const auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->CheckState(items);
        });
    }

    void StreamStateChecker::SetOnCheckedCallback(OnStreamStateCheckedCallback&& cbk) {
        on_checked_cbk_ = cbk;
    }

    void StreamStateChecker::CheckState(const std::vector<std::shared_ptr<px_console::ConsoleStream>>& items) {
        for (auto& item : items) {
            // Console application cards are catalog resources, not addressable
            // devices before a ticket is issued. Their state comes from the
            // application-instance API and must not be overwritten by an
            // empty host/port ping or a device-online query.
            if (item->connect_type_ == "console_app_ticket") {
                item->direct_online_ = item->console_instance_state_ == "running";
                item->relay_online_ = false;
                item->console_online_ = true;
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

            // check relay
            item->relay_online_ = false;
            if (item->HasRelayInfo()) {
                // to check in server
                auto device_info = context_->GetRelayServerSideDeviceInfo(item->relay_host_, item->relay_port_, grApp->GetAppkey()/*item->relay_appkey_*/, item->remote_device_id_, false);
                if (device_info && px_relay::RelayApi::IsRelayDeviceValid(device_info)) {
                    item->relay_online_ = true;
                }
            }

            // check console
            // online == the device holds a live panel connection to the console server,
            // NOT just a registered record in the database.
            item->console_online_ = false;
            if (!item->remote_device_id_.empty()) {
                item->console_online_ = px_console::ConsoleDeviceApi::IsDeviceOnline(settings_->GetConsoleServerHost(),
                                                                         settings_->GetConsoleServerPort(),
                                                                         grApp->GetAppkey(),
                                                                         item->remote_device_id_);
            }
        }

        if (on_checked_cbk_) {
            on_checked_cbk_(items);
        }
    }

}
