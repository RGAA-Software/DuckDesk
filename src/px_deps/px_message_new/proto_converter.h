//
// Created by RGAA on 5/07/2025.
//

#ifndef GAMMARAY_PROTO_CONVERTER_H
#define GAMMARAY_PROTO_CONVERTER_H

#include <memory>

namespace px
{
    class Data;
    class Message;

    std::shared_ptr<px::Data> ProtoAsData(std::shared_ptr<px::Message> msg);
    std::shared_ptr<px::Data> ProtoAsData(px::Message* msg);
}

#endif //GAMMARAY_PROTO_CONVERTER_H
