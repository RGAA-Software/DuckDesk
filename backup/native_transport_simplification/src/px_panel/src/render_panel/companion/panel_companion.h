//
// Created by RGAA on 6/08/2025.
//

#ifndef GAMMARAYPREMIUM_PANEL_COMPANION_H
#define GAMMARAYPREMIUM_PANEL_COMPANION_H

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>
#include "hw_info/hw_info.h"

namespace px
{

    enum class AuthRole {
        kFree = 1,
        kPersonal = 2,
        kEnterprise = 3,
    };

    // Authorization
    class Authorization {
    public:
        bool IsFree() const;
        bool IsPersonal() const;
        bool IsEnterprise() const;

    public:
        std::string auth_id_;
        std::string auth_name_;
        std::string machine_code_;
        std::string appkey_;
        AuthRole role_ = AuthRole::kFree;
        int days_ = 0;
        int max_streams_ = 0;
        int64_t end_timestamp_ms_ = 0;
    };

    // ConsoleSrvConfig
    class ConsoleSrvConfig {
    public:
        std::string srv_name_;
        std::string srv_w3c_ip_;
        int srv_console_port_ = 0;
        std::string srv_appkey_;
        int srv_relay_port_ = 0;
        // whether the console server requires ssl(https/wss), default true for old deployments
        bool srv_ssl_enable_ = true;

    public:
        [[nodiscard]] bool IsValid() const {
            return !srv_w3c_ip_.empty() && srv_console_port_ > 0 && !srv_appkey_.empty() && srv_relay_port_ > 0;
        }
    };

    // RelaySrvConfig
    class RelaySrvConfig {
    public:
        std::string srv_name_;
        std::string srv_type_;
        std::string srv_w3c_ip_;
        int srv_working_port_ = 0;
        std::string srv_appkey_;

    public:
        [[nodiscard]] bool IsValid() const {
            return !srv_w3c_ip_.empty() && srv_working_port_ > 0 && !srv_appkey_.empty();
        }
    };

    // Console Access
    class ConsoleAccessInfo {
    public:
        [[nodiscard]] bool IsValid() const {
            return console_config_.IsValid();
        }

    public:
        ConsoleSrvConfig console_config_;
    };

    //
    class PanelCompanion {
    public:
        virtual ~PanelCompanion();

        virtual bool Init() = 0;

        // Timer
        virtual void OnTimer100ms() = 0;
        virtual void OnTimer1S() = 0;
        virtual void OnTimer5S() = 0;

        // Console
        virtual void UpdateConsoleServerConfig(const std::string& host, int port, bool ssl_enable) = 0;
        virtual void UpdateAppkey(const std::string& appkey) = 0;
        virtual std::shared_ptr<Authorization> RequestAuth() = 0;
        virtual std::shared_ptr<Authorization> GetAuth() = 0;
        virtual bool IsAuthFree() = 0;
        virtual bool IsAuthPersonal() = 0;
        virtual bool IsAuthEnterprise() = 0;
        virtual bool IsAuthValid() = 0;

        // enc
        virtual bool EncQRCode(std::string origin_content, std::vector<uint8_t>& cipher_data) = 0;

        // parse hardware info
        virtual void UpdateCurrentCpuFrequency(float freq) = 0;
        virtual float GetCurrentCpuFrequency() = 0;
        virtual std::shared_ptr<SysInfo> ParseHardwareInfo(const std::string& info) = 0;

        // console access
        virtual std::shared_ptr<ConsoleAccessInfo> ParseConsoleAccessInfo(const std::string& info) = 0;

        // jump to github
        virtual void JumpToGithub() = 0;
        virtual bool HasUpdateForOffSite() = 0;

        // update device id
        virtual void UpdateDeviceId(const std::string& device_id) = 0;

    };

}

#endif //GAMMARAYPREMIUM_PANEL_COMPANION_H
