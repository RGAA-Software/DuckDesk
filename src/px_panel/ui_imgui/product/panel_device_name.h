#pragma once

#include <string>
#include <vector>

namespace px::panel::product {

std::string BuildDefaultDeviceName();
std::string BuildDefaultDeviceName(const std::vector<std::string>& ipv4Addresses);
bool IsManagedDeviceName(const std::string& value);

} // namespace px::panel::product
