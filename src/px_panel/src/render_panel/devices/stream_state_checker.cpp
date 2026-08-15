//
// Created by RGAA on 24/05/2025.
//

#include "stream_state_checker.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"
#include "render_panel/gr_context.h"
#include "render_panel/gr_app_messages.h"
#include "px_spvr_client/spvr_stream.h"
#include "relay_message.pb.h"
#include "px_relay_client/relay_api.h"
#include "render_panel/gr_settings.h"
#include "render_panel/gr_application.h"
#include "px_spvr_client/spvr_device_api.h"

namespace tc
{

    StreamStateChecker::StreamStateChecker(const std::shared_ptr<GrContext>& ctx) {
        context_ = ctx;
        msg_listener_ = context_->ObtainMessageListener();
        settings_ = GrSettings::Instance();
    }

    void StreamStateChecker::Start() {

    }

    void StreamStateChecker::Exit() {

    }

    void StreamStateChecker::UpdateCurrentStreamItems(const std::vector<std::shared_ptr<spvr::SpvrStream>>& items) {
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

    void StreamStateChecker::CheckState(const std::vector<std::shared_ptr<spvr::SpvrStream>>& items) {
        for (auto& item : items) {
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
                if (device_info && relay::RelayApi::IsRelayDeviceValid(device_info)) {
                    item->relay_online_ = true;
                }
            }

            // check spvr
            // online == the device holds a live panel connection to the spvr server,
            // NOT just a registered record in the database.
            item->spvr_online_ = false;
            if (!item->remote_device_id_.empty()) {
                item->spvr_online_ = spvr::SpvrDeviceApi::IsDeviceOnline(settings_->GetSpvrServerHost(),
                                                                         settings_->GetSpvrServerPort(),
                                                                         grApp->GetAppkey(),
                                                                         item->remote_device_id_);
            }
        }

        if (on_checked_cbk_) {
            on_checked_cbk_(items);
        }
    }

}
