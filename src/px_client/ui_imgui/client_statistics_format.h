#pragma once

#include <string>
#include <vector>

#include "client_statistics.h"
#include "client_text.h"

namespace px::client::imgui {

struct ClientStatisticsRow final {
    ClientText label{};
    std::string value{};
};

[[nodiscard]] std::vector<ClientStatisticsRow> BuildClientStatisticsRows(const ClientStatisticsSnapshot& statistics, bool english);

}  // namespace px::client::imgui
