//
// Created by RGAA on 6/07/2025.
//

#ifndef GAMMARAY_RP_PROTO_CONVERTER_H
#define GAMMARAY_RP_PROTO_CONVERTER_H

#include <memory>
#include <string>

namespace tcrp
{
    class RpMessage;
}

namespace tc
{
    class Data;

    std::shared_ptr<tc::Data> RpProtoAsData(std::shared_ptr<tcrp::RpMessage> msg);
    std::shared_ptr<tc::Data> RpProtoAsData(tcrp::RpMessage *msg);

    std::shared_ptr<tc::Data> MakeRpRawRenderMessage(const std::string& stream_id,
                                                     const std::string& device_id,
                                                     const std::string& msg,
                                                     bool data_channel,
                                                     bool run_through = false);
}
#endif //GAMMARAY_RP_PROTO_CONVERTER_H
