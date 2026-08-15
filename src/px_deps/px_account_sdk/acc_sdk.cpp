//
// Created by RGAA on 30/05/2025.
//

#include "acc_sdk.h"
#include <format>
#include "px_common_new/message_notifier.h"
#include "acc_device.h"
#include "acc_profile.h"

namespace px
{

    AccountSdk::AccountSdk(const std::shared_ptr<MessageNotifier>& notifier,
                           const std::shared_ptr<AccountParams>& params) {
        msg_notifier_ = notifier;
        acc_params_ = params;
    }

    std::vector<std::shared_ptr<AccountDevice>> AccountSdk::QueryDevices(const std::string& acc_id, int page, int page_size) {
        std::vector<std::shared_ptr<AccountDevice>> devices;
        for (int i = 0; i < 20; i++) {
            devices.push_back(std::make_shared<AccountDevice>(AccountDevice {
                .device_id_ = std::format("id: {}", i),
                .device_desktop_name_ = std::format("desktop: {}", i),
                .device_os_ = std::format("os: {}", i),
                .device_custom_name_ = std::format("custom:{}", i),
            }));
        }
        return devices;
    }

}