//
// Created by RGAA on 6/07/2025.
//
#include "rp_proto_converter.h"
#include "tc_common_new/data.h"
#include "tc_render_panel_message.pb.h"

namespace tc
{

    std::shared_ptr<tc::Data> RpProtoAsData(std::shared_ptr<tcrp::RpMessage> msg) {
        return RpProtoAsData(msg.get());
    }

    std::shared_ptr<tc::Data> RpProtoAsData(tcrp::RpMessage* msg) {
        if (!msg) {
            return nullptr;
        }
        auto buffer = Data::Make(nullptr, msg->ByteSizeLong());
        if (auto ok = msg->SerializeToArray(buffer->DataAddr(), buffer->Size()); ok) {
            return buffer;
        }
        return nullptr;
    }

    std::shared_ptr<tc::Data> MakeRpRawRenderMessage(const std::string& stream_id,
                                                     const std::string& device_id,
                                                     const std::string& msg,
                                                     bool data_channel,
                                                     bool run_through) {
        tcrp::RpMessage rp_msg;
        rp_msg.set_type(tcrp::kRpRawRenderMessage);
        auto sub = rp_msg.mutable_raw_render_msg();
        sub->set_msg(msg);
        sub->set_data_channel(data_channel);
        sub->set_stream_id(stream_id);
        sub->set_device_id(device_id);
        sub->set_run_through(run_through);
        return RpProtoAsData(&rp_msg);
    }

}