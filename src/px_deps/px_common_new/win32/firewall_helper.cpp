#include "firewall_helper.h"
#include <Windows.h>
#include <netfw.h>
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

//NET_FW_RULE_DIRECTION_ GetRuleType(int type);
std::string GetErrorMessage(HRESULT hr);

#pragma comment(lib, "ole32.lib" )
#pragma comment(lib, "oleaut32.lib" )

namespace tc
{

    FirewallHelper::FirewallHelper() {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            LOGE("CoInitializeEx failed");
            return;
        }
        hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), (void **) &fw_policy2);
        if (FAILED(hr)) {
            LOGE("CoCreateInstance failed");
            return;
        }
        is_init = true;
    }

    FirewallHelper::~FirewallHelper() {
        CoUninitialize();
    }

    FirewallHelper *FirewallHelper::Instance() {
        static FirewallHelper gFirewall;
        return &gFirewall;
    }

    bool FirewallHelper::AddProgramToFirewall(const RulesInfo& i) {
        RulesInfo info = i;
        bool success = false;
        if (!is_init) {
            return success;
        }

        std::lock_guard<std::mutex> lock(lock_mutex);
        hr = fw_policy2->get_Rules(&fw_rules);
        if (SUCCEEDED(hr)) {
            INetFwRule *fw_rule_item = nullptr;
            std::wstring name = StringUtil::ToWString(info.name);
            hr = fw_rules->Item((wchar_t *) name.c_str(), &fw_rule_item);
            if (SUCCEEDED(hr)) {
                success = true;
                fw_rule_item->Release();
                fw_rules->Release();
                return success;
            }

            hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
                                  (void **) &fw_rule_item);

            if (SUCCEEDED(hr)) {
                auto pos = info.program_path.find("/");
                while (pos != std::string::npos) {
                    info.program_path = info.program_path.replace(pos, 1, "\\");
                    pos = info.program_path.find("/");
                }

                std::wstring path = StringUtil::ToWString(info.program_path);
                std::wstring desc = StringUtil::ToWString(info.desc);

                fw_rule_item->put_Name((wchar_t *) name.c_str());
                fw_rule_item->put_ApplicationName((wchar_t *) path.c_str());
                fw_rule_item->put_Description((wchar_t *) desc.c_str());
                fw_rule_item->put_Action(info.is_allow ? NET_FW_ACTION_ALLOW : NET_FW_ACTION_BLOCK);
                fw_rule_item->put_Direction((NET_FW_RULE_DIRECTION)info.type/*GetRuleType(info.type)*/);
                fw_rule_item->put_Enabled(VARIANT_TRUE);
                fw_rule_item->put_InterfaceTypes(SysAllocString(L"All"));
                //fw_rule_item->put_LocalPorts(L"*");
                fw_rule_item->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
                fw_rule_item->put_Profiles(NET_FW_PROFILE2_ALL);
                hr = fw_rules->Add(fw_rule_item);

                //GetErrorMessage(hr);

                if (SUCCEEDED(hr)) {
                    success = true;
                }
            }
            fw_rule_item->Release();
            fw_rules->Release();
        }
        return success;
    }

    bool FirewallHelper::AddPortToFirewall(const std::string& rule_name, const std::string& local_ports, int protocol, int direction) {
        bool success = false;
        if (!is_init) {
            return success;
        }

        std::lock_guard<std::mutex> lock(lock_mutex);
        hr = fw_policy2->get_Rules(&fw_rules);
        if (SUCCEEDED(hr)) {
            INetFwRule *fw_rule_item = nullptr;
            std::wstring name = StringUtil::ToWString(rule_name);
            hr = fw_rules->Item((wchar_t *) name.c_str(), &fw_rule_item);
            if (SUCCEEDED(hr)) {
                success = true;
                fw_rule_item->Release();
                fw_rules->Release();
                return success;
            }

            hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
                                  (void **) &fw_rule_item);

            if (SUCCEEDED(hr)) {
                std::wstring ports = StringUtil::ToWString(local_ports);
                std::wstring desc = StringUtil::ToWString("GammaRay local rtc port rule");

                fw_rule_item->put_Name((wchar_t *) name.c_str());
                fw_rule_item->put_Description((wchar_t *) desc.c_str());
                fw_rule_item->put_Action(NET_FW_ACTION_ALLOW);
                fw_rule_item->put_Direction((NET_FW_RULE_DIRECTION)direction);
                fw_rule_item->put_Enabled(VARIANT_TRUE);
                fw_rule_item->put_InterfaceTypes(SysAllocString(L"All"));
                fw_rule_item->put_Protocol(protocol);
                fw_rule_item->put_LocalPorts((wchar_t *) ports.c_str());
                fw_rule_item->put_Profiles(NET_FW_PROFILE2_ALL);
                hr = fw_rules->Add(fw_rule_item);

                if (SUCCEEDED(hr)) {
                    success = true;
                }
            }
            fw_rule_item->Release();
            fw_rules->Release();
        }
        return success;
    }

    bool FirewallHelper::RemoveProgramFromFirewall(const std::string &rule_name) {
        bool success = false;
        if (!is_init) {
            return success;
        }
        std::lock_guard<std::mutex> lock(lock_mutex);
        std::wstring name = StringUtil::ToWString(rule_name);
        hr = fw_policy2->get_Rules(&fw_rules);
        if (SUCCEEDED(hr)) {
            INetFwRule *fw_rule_item = nullptr;
            hr = fw_rules->Item((wchar_t *) name.c_str(), &fw_rule_item);
            while (SUCCEEDED(hr)) {
                fw_rule_item->Release();
                fw_rules->Remove((wchar_t *) name.c_str());
                success = true;
                hr = fw_rules->Item((wchar_t *) name.c_str(), &fw_rule_item);
            }
            fw_rules->Release();
        }

        return success;
    }

//    NET_FW_RULE_DIRECTION_ GetRuleType(int type) {
//        switch (type) {
//            case 1:
//                return NET_FW_RULE_DIR_IN;
//            case 2:
//                return NET_FW_RULE_DIR_OUT;
//        }
//        return NET_FW_RULE_DIR_MAX;
//    }

    std::string GetErrorMessage(HRESULT hr) {
        LPSTR errorMessageBuffer = nullptr;
        DWORD errorMessageLength = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                hr,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&errorMessageBuffer),
                0,
                nullptr
        );

        std::string error_message(errorMessageBuffer, errorMessageLength);
        LOGE("Error: {}", error_message);
        LocalFree(errorMessageBuffer);
        return error_message;
    }

}
