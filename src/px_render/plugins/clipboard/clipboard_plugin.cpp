//
// Created RGAA on 15/11/2024.
//

#include "clipboard_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_opus_codec_new/opus_codec.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/file.h"
#include "px_common_new/time_util.h"
#include "clipboard_manager.h"
#include "px_message.pb.h"
#include "win/cp_virtual_file.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_message_new/proto_converter.h"
#include "px_common_new/md5.h"

extern "C" {
    __declspec(dllimport) uint64_t GenNextGlobalId();
}

PX_PLUGIN_EXPORT(px::ClipboardPlugin)

namespace px
{
    std::string ClipboardPlugin::GetPluginId() {
        return kClipboardPluginId;
    }

    std::string ClipboardPlugin::GetPluginName() {
        return "Clipboard";
    }

    std::string ClipboardPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t ClipboardPlugin::GetVersionCode() {
        return 110;
    }

    std::string ClipboardPlugin::GetPluginDescription() {
        return "Share clipboard between devices";
    }

    void ClipboardPlugin::On1Second() {

    }

    bool ClipboardPlugin::OnCreate(const px::PxPluginParam &param) {
        PxPluginInterface::OnCreate(param);
        lifetime_token_->store(true);
        clipboard_mgr_ = std::make_shared<ClipboardManager>(this);

        // test //
        for (int i = 0; i < 10; i++) {
            LOGI("NextGlobalId: {}", GenNextGlobalId());
        }
        // test //

        return true;
    }

    bool ClipboardPlugin::OnDestroy() {
        lifetime_token_->store(false);
        PxPluginInterface::OnStop();
        return PxPluginInterface::OnDestroy();
    }

    void ClipboardPlugin::OnMessage(std::shared_ptr<Message> msg) {
        if (msg->type() == MessageType::kClipboardInfoResp) {
            // tell the panel, remote info
            auto event = std::make_shared<PxPluginRemoteClipboardResp>();
            auto sub = msg->clipboard_info_resp();
            event->content_type_ = (int)sub.type();
            event->remote_info_ = sub.msg();
            CallbackEvent(event);
            LOGI("received clipboard resp: {}", sub.msg());
        }
        else if (msg->type() == px::kClipboardReqAtBegin) {
            // begin; server -> client
            // copy files from server -> client
            plugin_context_->PostWorkTask([=, this]() {
                this->OnRequestFileBegin(msg);
            });
        }
        else if (msg->type() == px::kClipboardReqAtEnd) {
            // end; server -> client
            // copy files from server -> client
            plugin_context_->PostWorkTask([=, this]() {
                this->OnRequestFileEnd(msg);
            });
        }
        else if (msg->type() == MessageType::kClipboardReqBuffer) {
            // copy file buffer, from server -> client
            this->OnRequestFileBuffer(msg);
        }
    }

    void ClipboardPlugin::OnRequestFileBegin(std::shared_ptr<Message> msg) {
        auto sub = msg->cp_req_at_begin();
        auto event = std::make_shared<PxPluginFileTransferBegin>();
        event->the_file_id_ = MD5::Hex(sub.full_name());
        event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->visitor_device_id_ = msg->device_id();
        event->direction_ = "Out";
        event->file_detail_ = sub.full_name();
        CallbackEvent(event);
    }

    void ClipboardPlugin::OnRequestFileEnd(std::shared_ptr<Message> msg) {
        auto sub = msg->cp_req_at_end();
        auto event = std::make_shared<PxPluginFileTransferEnd>();
        event->the_file_id_ = MD5::Hex(sub.full_name());
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->success_ = true;
        CallbackEvent(event);
    }

    void ClipboardPlugin::DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event) {
        if (event->type_ == AppBaseEvent::EType::kClipboardEvent) {
            if (auto ev = std::dynamic_pointer_cast<MsgClipboardEvent>(event); ev) {
                plugin_context_->PostUITask([=, this]() {
                    clipboard_mgr_->OnLocalClipboardUpdated(ev);
                });
            }
        }
    }

    void ClipboardPlugin::OnRequestFileBuffer(std::shared_ptr<Message> in_msg) {
        const auto& buffer = in_msg->cp_req_buffer();
        auto req_index = buffer.req_index();
        auto req_start = buffer.req_start();
        auto req_size = buffer.req_size();
        auto full_filename = buffer.full_name();

        auto file = File::OpenForReadB(U8Path(full_filename));
        DataPtr data = nullptr;
        if (file->Exists()) {
            uint64_t read_size = 0;
            data = file->Read(req_start, req_size, read_size);
        }

        px::Message msg;
        msg.set_device_id(sys_settings_.device_id_);
        msg.set_stream_id(in_msg->stream_id());
        msg.set_type(MessageType::kClipboardRespBuffer);
        auto sub = msg.mutable_cp_resp_buffer();
        sub->set_full_name(full_filename);
        sub->set_req_size(req_size);
        sub->set_req_start(req_start);
        sub->set_req_index(req_index);
        if (data) {
            sub->set_read_size(data->Size());
            sub->set_buffer(data->AsString());
        }
        auto mb = ProtoAsData(&msg);
        this->DispatchTargetFileTransferMessage(in_msg->stream_id(), mb);
        //LOGI("Req, index: {}, start: {}, size: {}, read size: {}", req_index, req_start, req_size, data ? data->Size() : 0);
    }
}
