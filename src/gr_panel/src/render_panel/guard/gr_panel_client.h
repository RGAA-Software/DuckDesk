//
// Created by RGAA on 1/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_GUARD_PANEL_CLIENT_H
#define GAMMARAYPREMIUM_GR_GUARD_PANEL_CLIENT_H

#include <atomic>
#include <asio2/asio2.hpp>

namespace tc
{

    class GrPanelClient : public std::enable_shared_from_this<GrPanelClient> {
    public:
        void Start();
        void Exit();

    private:
        std::shared_ptr<asio2::wss_client> client_ = nullptr;
        std::atomic_bool exiting_ = false;

    };

}

#endif //GAMMARAYPREMIUM_GR_PANEL_CLIENT_H
