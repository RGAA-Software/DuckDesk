//
// Created by RGAA on 2025.
//

#include <gtest/gtest.h>
#include <string>
#include <algorithm>
#include <ranges>

#include "../ip_util.h"

using namespace tc;

TEST(NetworkAdapterTest, ScanIPsNotEmpty) {
    auto ips = IPUtil::ScanIPs();
    if (ips.empty()) {
        GTEST_SKIP() << "No network adapters found in this environment";
    }
    EXPECT_FALSE(ips.empty()) << "Expected at least one network adapter";
}

TEST(NetworkAdapterTest, FilterLogic) {
    auto ips = IPUtil::ScanIPs();
    if (ips.empty()) {
        GTEST_SKIP() << "No network adapters found in this environment";
    }

    for (const auto& info : ips) {
        std::string name_lower = info.human_readable_name_;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c){ return std::tolower(c); });

        EXPECT_EQ(name_lower.find("vmware"), std::string::npos)
            << "VMware adapter should be filtered out: " << info.human_readable_name_;
        EXPECT_EQ(name_lower.find("loopback"), std::string::npos)
            << "Loopback adapter should be filtered out: " << info.human_readable_name_;
        EXPECT_EQ(name_lower.find("virtualbox"), std::string::npos)
            << "VirtualBox adapter should be filtered out: " << info.human_readable_name_;
        EXPECT_EQ(name_lower.find("wsl"), std::string::npos)
            << "WSL adapter should be filtered out: " << info.human_readable_name_;
    }
}

TEST(NetworkAdapterTest, MacAddressFormat) {
    auto ips = IPUtil::ScanIPs();
    if (ips.empty()) {
        GTEST_SKIP() << "No network adapters found in this environment";
    }

    for (const auto& info : ips) {
        EXPECT_FALSE(info.mac_address_.empty()) << "MAC address should not be empty";
        EXPECT_EQ(info.mac_address_.size(), 17)
            << "MAC address should be 17 chars (XX:XX:XX:XX:XX:XX): " << info.mac_address_;

        for (size_t i = 0; i < info.mac_address_.size(); i++) {
            char c = info.mac_address_[i];
            if (i % 3 == 2) {
                EXPECT_EQ(c, ':') << "MAC address separator should be colon at position " << i;
            } else {
                EXPECT_TRUE(std::isxdigit(c)) << "MAC address should be hex digit at position " << i;
            }
        }
    }
}

TEST(NetworkAdapterTest, Ipv4AddressValid) {
    auto ips = IPUtil::ScanIPs();
    if (ips.empty()) {
        GTEST_SKIP() << "No network adapters found in this environment";
    }

    for (const auto& info : ips) {
        EXPECT_FALSE(info.ip_addr_.empty()) << "IP address should not be empty";
        EXPECT_NE(info.ip_addr_.find('.'), std::string::npos)
            << "Should be IPv4 address with dots: " << info.ip_addr_;
    }
}


