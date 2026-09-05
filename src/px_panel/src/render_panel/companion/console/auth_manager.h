//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_AUTH_MANAGER_H
#define GAMMARAYPREMIUM_AUTH_MANAGER_H

#include <functional>
#include <memory>
#include "px_common/concurrent_type.h"

namespace px
{

    class Authorization;
    class SharedPreference;

    class AuthManager {
    public:
        explicit AuthManager(std::shared_ptr<SharedPreference> storage);
        std::shared_ptr<Authorization> RequestAuth();
        std::shared_ptr<Authorization> GetAuth() const;
        bool IsAuthValid() const;
        void LoadFromStorage();
        void FlushToStorage();
        void UpdateAppkey(const std::string& appkey);

    private:
        std::shared_ptr<SharedPreference> storage_;
        px::Mutex<std::shared_ptr<Authorization>> auth_;
    };

    std::function<void()> MakeAuthRefreshTask(
        const std::shared_ptr<AuthManager>& manager);

}

#endif //GAMMARAYPREMIUM_AUTH_MANAGER_H
