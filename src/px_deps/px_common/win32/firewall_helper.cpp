#include "firewall_helper.h"

#include <Windows.h>
#include <comutil.h>
#include <netfw.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>

#include "px_common/log.h"
#include "px_common/string_util.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace px {
namespace {

using Microsoft::WRL::ComPtr;

class ComApartment final {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool Ready() const noexcept { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
    [[nodiscard]] HRESULT Result() const noexcept { return result_; }

private:
    HRESULT result_{};
};

ComPtr<INetFwRules> OpenRules() {
    ComPtr<INetFwPolicy2> policy;
    const HRESULT create_result = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&policy));
    if (FAILED(create_result)) {
        LOGE("event=firewall.open stage=create_policy outcome=failed hresult={:#x}", static_cast<std::uint32_t>(create_result));
        return {};
    }
    ComPtr<INetFwRules> rules;
    const HRESULT rules_result = policy->get_Rules(&rules);
    if (FAILED(rules_result)) {
        LOGE("event=firewall.open stage=get_rules outcome=failed hresult={:#x}", static_cast<std::uint32_t>(rules_result));
        return {};
    }
    return rules;
}

bool RuleExists(const ComPtr<INetFwRules>& rules, const std::wstring& name) {
    ComPtr<INetFwRule> existing;
    return SUCCEEDED(rules->Item(_bstr_t{name.c_str()}, &existing));
}

ComPtr<INetFwRule> CreateRule() {
    ComPtr<INetFwRule> rule;
    const HRESULT result = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rule));
    if (FAILED(result)) {
        LOGE("event=firewall.rule stage=create outcome=failed hresult={:#x}", static_cast<std::uint32_t>(result));
        return {};
    }
    return rule;
}

}  // namespace

bool FirewallHelper::AddProgramToFirewall(const RulesInfo& info) {
    const ComApartment apartment;
    if (!apartment.Ready()) {
        LOGE("event=firewall.program stage=com_init outcome=failed hresult={:#x}", static_cast<std::uint32_t>(apartment.Result()));
        return false;
    }
    const auto rules = OpenRules();
    if (!rules) {
        return false;
    }

    const auto name = StringUtil::ToWString(info.name);
    if (RuleExists(rules, name)) {
        return true;
    }
    const auto rule = CreateRule();
    if (!rule) {
        return false;
    }

    auto normalized_path = info.program_path;
    std::replace(normalized_path.begin(), normalized_path.end(), '/', '\\');
    const auto path = StringUtil::ToWString(normalized_path);
    const auto description = StringUtil::ToWString(info.desc);
    rule->put_Name(_bstr_t{name.c_str()});
    rule->put_ApplicationName(_bstr_t{path.c_str()});
    rule->put_Description(_bstr_t{description.c_str()});
    rule->put_Action(info.is_allow != 0 ? NET_FW_ACTION_ALLOW : NET_FW_ACTION_BLOCK);
    rule->put_Direction(static_cast<NET_FW_RULE_DIRECTION>(info.type));
    rule->put_Enabled(info.enable != 0 ? VARIANT_TRUE : VARIANT_FALSE);
    rule->put_InterfaceTypes(_bstr_t{L"All"});
    rule->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
    rule->put_Profiles(NET_FW_PROFILE2_ALL);
    const HRESULT result = rules->Add(rule.Get());
    if (FAILED(result)) {
        LOGE("event=firewall.program stage=add outcome=failed name={} hresult={:#x}", info.name, static_cast<std::uint32_t>(result));
        return false;
    }
    LOGI("event=firewall.program outcome=success name={}", info.name);
    return true;
}

bool FirewallHelper::AddPortToFirewall(const std::string& rule_name, const std::string& local_ports, int protocol, int direction) {
    const ComApartment apartment;
    if (!apartment.Ready()) {
        LOGE("event=firewall.port stage=com_init outcome=failed hresult={:#x}", static_cast<std::uint32_t>(apartment.Result()));
        return false;
    }
    const auto rules = OpenRules();
    if (!rules) {
        return false;
    }

    const auto name = StringUtil::ToWString(rule_name);
    if (RuleExists(rules, name)) {
        return true;
    }
    const auto rule = CreateRule();
    if (!rule) {
        return false;
    }

    const auto ports = StringUtil::ToWString(local_ports);
    rule->put_Name(_bstr_t{name.c_str()});
    rule->put_Description(_bstr_t{L"px local rtc port rule"});
    rule->put_Action(NET_FW_ACTION_ALLOW);
    rule->put_Direction(static_cast<NET_FW_RULE_DIRECTION>(direction));
    rule->put_Enabled(VARIANT_TRUE);
    rule->put_InterfaceTypes(_bstr_t{L"All"});
    rule->put_Protocol(protocol);
    rule->put_LocalPorts(_bstr_t{ports.c_str()});
    rule->put_Profiles(NET_FW_PROFILE2_ALL);
    const HRESULT result = rules->Add(rule.Get());
    if (FAILED(result)) {
        LOGE("event=firewall.port stage=add outcome=failed name={} hresult={:#x}", rule_name, static_cast<std::uint32_t>(result));
        return false;
    }
    LOGI("event=firewall.port outcome=success name={} ports={} protocol={} direction={}", rule_name, local_ports, protocol, direction);
    return true;
}

bool FirewallHelper::RemoveProgramFromFirewall(const std::string& rule_name) {
    const ComApartment apartment;
    if (!apartment.Ready()) {
        LOGE("event=firewall.remove stage=com_init outcome=failed hresult={:#x}", static_cast<std::uint32_t>(apartment.Result()));
        return false;
    }
    const auto rules = OpenRules();
    if (!rules) {
        return false;
    }
    const HRESULT result = rules->Remove(_bstr_t{StringUtil::ToWString(rule_name).c_str()});
    if (FAILED(result)) {
        LOGE("event=firewall.remove outcome=failed name={} hresult={:#x}", rule_name, static_cast<std::uint32_t>(result));
        return false;
    }
    LOGI("event=firewall.remove outcome=success name={}", rule_name);
    return true;
}

}  // namespace px
