//
// Created by RGAA on 23/10/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_SCANNER_H
#define GAMMARAYPREMIUM_CMS_SCANNER_H

#include <map>
#include <memory>
#include <string>
#include <mutex>

namespace px
{

    class Thread;
    class GrApplication;
    class MessageListener;

    class StNetworkCmsAccessInfo {
    public:
        std::string cms_ip_;
        int cms_port_;
        std::string relay_ip_;
        int relay_port_;
        std::string origin_info_;
        int64_t update_timestamp_ = 0;
    };

    class CmsScanner {
    public:
        explicit CmsScanner(const std::shared_ptr<GrApplication>& app);
        //
        void StartUdpReceiver(int port);
        void Exit();
        std::map<std::string, std::shared_ptr<StNetworkCmsAccessInfo>> GetCmsAccessInfo();

    private:
        void ClearInactiveServer();

    private:
        std::shared_ptr<GrApplication> app_ = nullptr;
        // udp receiver thread
        std::shared_ptr<Thread> udp_receiver_thread_ = nullptr;
        std::atomic_bool exit_udp_receiver_ = false;
        std::mutex ac_mtx_;
        std::map<std::string, std::shared_ptr<StNetworkCmsAccessInfo>> access_info_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_CMS_SCANNER_H
