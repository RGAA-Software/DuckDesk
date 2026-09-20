#pragma once

#include <memory>
#include <string>

#include "px_render/network/transport_types.h"

namespace px {

class Data;

class RelayResourceChannel final {
public:
    [[nodiscard]] static ConsoleResourceChannelKind Classify(const std::shared_ptr<const Data>& payload);
    [[nodiscard]] static std::string ConnectionId(const std::string& connection_instance_id,
                                                  ConsoleResourceChannelKind channel_kind);
};

}  // namespace px
