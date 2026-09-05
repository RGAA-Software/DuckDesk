//
// Created by RGAA on 5/07/2025.
//

#include "proto_converter.h"
#include "px_common_new/data.h"
#include "px_message.pb.h"

namespace px
{

    std::shared_ptr<px::Data> ProtoAsData(std::shared_ptr<px::Message> msg) {
        return ProtoAsData(msg.get());
    }

    std::shared_ptr<px::Data> ProtoAsData(px::Message* msg) {
        if (!msg) {
            return nullptr;
        }
        auto buffer = Data::Allocate( msg->ByteSizeLong());
        if (auto ok = msg->SerializeToArray(buffer->MutableBytes().data(), buffer->Size()); ok) {
            return buffer;
        }
        return nullptr;
    }

}