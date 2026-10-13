#pragma once

#include <optional>
#include <string>

#include "px_transport/transport.h"

namespace px {
struct IrohTransportStatistics final {
    transport::PathKind path{transport::PathKind::kUnknown};
    std::string selectedAddress{};
    std::optional<double> roundTripMilliseconds{};
};
}  // namespace px
