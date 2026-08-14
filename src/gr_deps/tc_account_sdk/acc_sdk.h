//
// Created by RGAA on 30/05/2025.
//

#ifndef GAMMARAY_ACC_SDK_H
#define GAMMARAY_ACC_SDK_H

#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace tc
{

    class MessageNotifier;
    class AccountProfile;
    class AccountDevice;

    // params
    class AccountParams {
    public:
        std::string host_;
        int port_{0};
    };

    // sdk
    class AccountSdk {
    public:
        explicit AccountSdk(const std::shared_ptr<MessageNotifier>& notifier,
                            const std::shared_ptr<AccountParams>& params);

        std::vector<std::shared_ptr<AccountDevice>> QueryDevices(const std::string& acc_id, int page, int page_size);

    private:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<AccountParams> acc_params_ = nullptr;
    };

}

#endif //GAMMARAY_ACC_SDK_H
