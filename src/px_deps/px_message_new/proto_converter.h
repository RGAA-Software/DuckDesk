//
// Created by RGAA on 5/07/2025.
//

#ifndef PX_PROTO_CONVERTER_H
#define PX_PROTO_CONVERTER_H

#include <memory>

namespace px
{
    class Data;
    class Message;

    std::shared_ptr<px::Data> ProtoAsData(std::shared_ptr<px::Message> msg);
    std::shared_ptr<px::Data> ProtoAsData(px::Message* msg);
}

#endif //PX_PROTO_CONVERTER_H
