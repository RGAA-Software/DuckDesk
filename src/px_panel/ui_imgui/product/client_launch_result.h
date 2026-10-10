#pragma once

#include "px_ui/localization.h"

namespace px::panel::product {

struct ClientLaunchResult final {
    bool connected{};
    px::ui::TextId error{px::ui::TextId::ConnectionClientLaunchFailed};

    ClientLaunchResult() = default;
    ClientLaunchResult(bool success) : connected{success} {}
    ClientLaunchResult(px::ui::TextId failure) : error{failure} {}
    operator bool() const noexcept { return connected; }
};

}  // namespace px::panel::product
