//
// Created by RGAA on 2023/8/16.
//

#include "ip_util.h"

#include "tc_common_new/log.h"
#include "tc_common_new/string_util.h"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iptypes.h>
#include <format>
#include <algorithm>
#include <ranges>

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace tc
{

    static bool NeedIgnoreNetwork(std::wstring_view str_card_name) {
        constexpr std::wstring_view kIgnoreList[] = {
            L"VMware", L"Loopback", L"VirtualBox", L"WSL"
        };
        return std::ranges::any_of(kIgnoreList, [&](std::wstring_view keyword) {
            return str_card_name.find(keyword) != std::wstring_view::npos;
        });
    }

    // 0 - wireless , 1 - wire
    static void GetIps(std::vector<EthernetInfo>& all_et_info) {
        ULONG bufLen = 0;
        DWORD ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen);
        if (ret != ERROR_BUFFER_OVERFLOW) {
            return;
        }

        std::vector<uint8_t> buffer(bufLen);
        auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &bufLen);
        if (ret != ERROR_SUCCESS) {
            LOGE("GetAdaptersAddresses failed: {}", ret);
            return;
        }

        for (auto* curr = adapter; curr != nullptr; curr = curr->Next) {
            std::wstring_view friendly_name = curr->FriendlyName;
            if (NeedIgnoreNetwork(friendly_name)) {
                continue;
            }

            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK || curr->IfType == IF_TYPE_PPP) {
                continue;
            }

            if (curr->OperStatus != IfOperStatusUp) {
                continue;
            }

            std::string mac_address;
            for (DWORD i = 0; i < curr->PhysicalAddressLength; i++) {
                if (i > 0) mac_address.push_back(':');
                mac_address += std::format("{:02X}", static_cast<int>(curr->PhysicalAddress[i]));
            }

            for (auto* unicast = curr->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                if (unicast->Address.lpSockaddr->sa_family != AF_INET) {
                    continue;
                }

                char ip_str[INET_ADDRSTRLEN] = {0};
                auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                inet_ntop(AF_INET, &sin->sin_addr, ip_str, INET_ADDRSTRLEN);

                std::string adapter_name = curr->AdapterName ? curr->AdapterName : "";
                std::string friendly_name_utf8 = StringUtil::ToUTF8(curr->FriendlyName);

                std::string name_lower = friendly_name_utf8;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                std::string adapter_lower = adapter_name;
                std::transform(adapter_lower.begin(), adapter_lower.end(), adapter_lower.begin(), [](unsigned char c){ return std::tolower(c); });

                IPNetworkType nt_type = IPNetworkType::kWired;
                std::string combined = adapter_lower + " " + name_lower;
                if (combined.find("wireless") != std::string::npos
                    || combined.find("wi-fi") != std::string::npos
                    || combined.find("wlan") != std::string::npos) {
                    nt_type = IPNetworkType::kWireless;
                }

                all_et_info.push_back(EthernetInfo{
                    .human_readable_name_ = friendly_name_utf8,
                    .ip_addr_ = ip_str,
                    .nt_type_ = nt_type,
                    .mac_address_ = mac_address,
                });
            }
        }
    }

    std::vector<EthernetInfo> IPUtil::ScanIPs() {
        std::vector<EthernetInfo> ips;
        GetIps(ips);
        return ips;
    }

}
