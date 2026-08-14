//
// Created by RGAA on 5/07/2025.
//

#include "proto_converter.h"
#include "tc_common_new/data.h"
#include "tc_message.pb.h"

namespace tc
{

    std::shared_ptr<tc::Data> ProtoAsData(std::shared_ptr<tc::Message> msg) {
        return ProtoAsData(msg.get());
    }

    std::shared_ptr<tc::Data> ProtoAsData(tc::Message* msg) {
        if (!msg) {
            return nullptr;
        }
        auto buffer = Data::Make(nullptr, msg->ByteSizeLong());
        if (auto ok = msg->SerializeToArray(buffer->DataAddr(), buffer->Size()); ok) {
            return buffer;
        }
        return nullptr;
    }

}