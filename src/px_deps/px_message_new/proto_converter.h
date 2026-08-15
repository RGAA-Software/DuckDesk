//
// Created by RGAA on 5/07/2025.
//

#ifndef GAMMARAY_PROTO_CONVERTER_H
#define GAMMARAY_PROTO_CONVERTER_H

#include <memory>

namespace tc
{
    class Data;
    class Message;

    std::shared_ptr<tc::Data> ProtoAsData(std::shared_ptr<tc::Message> msg);
    std::shared_ptr<tc::Data> ProtoAsData(tc::Message* msg);
}

#endif //GAMMARAY_PROTO_CONVERTER_H
