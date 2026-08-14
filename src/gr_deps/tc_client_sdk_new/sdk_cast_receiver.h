//
// Created by RGAA on 2024/3/15.
//

#ifndef TC_CLIENT_PC_CAST_RECEIVER_H
#define TC_CLIENT_PC_CAST_RECEIVER_H

//#include <asio2/udp/udp_cast.hpp>
#include <thread>
#include <memory>

namespace tc
{

    class CastReceiver {
    public:

        static std::shared_ptr<CastReceiver> Make();

        CastReceiver();

        void Start();
        void Exit();

    private:

        std::thread recv_thread_;
        //std::shared_ptr<asio2::udp_cast> udp_cast_ = nullptr;
    };

}

#endif //TC_CLIENT_PC_CAST_RECEIVER_H
