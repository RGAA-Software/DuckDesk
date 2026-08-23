//
// Created by RGAA on 29/08/2025.
//

#ifndef GAMMARAYPREMIUM_AUTH_MANAGER_H
#define GAMMARAYPREMIUM_AUTH_MANAGER_H

#include <memory>
#include "px_common_new/concurrent_type.h"

namespace px
{

    class Authorization;
    class PanelCompanionImpl;

    class AuthManager {
    public:
        explicit AuthManager(PanelCompanionImpl* pc);
        void OnTimer5S();
        std::shared_ptr<Authorization> RequestAuth();
        std::shared_ptr<Authorization> GetAuth() const;
        bool IsAuthValid() const;
        void LoadFromStorage();
        void FlushToStorage();
        void UpdateAppkey(const std::string& appkey);

    private:

    private:
        PanelCompanionImpl* pc_ = nullptr;
        px::Mutex<std::shared_ptr<Authorization>> auth_;
    };

}

#endif //GAMMARAYPREMIUM_AUTH_MANAGER_H
