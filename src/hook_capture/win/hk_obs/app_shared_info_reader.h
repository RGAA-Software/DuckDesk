//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_APP_SHARED_INFO_H
#define TC_APPLICATION_APP_SHARED_INFO_H

#include <map>
#include <memory>
#include <functional>
#include <string>

//#include <Poco/NamedEvent.h>
//#include <Poco/SharedMemory.h>
//#include <Poco/NamedMutex.h>

#include "tc_capture_new/capture_message.h"

namespace tc
{

    // write information to shared memory, so the dll can read the shm after it is injected.
    class AppSharedInfoReader {
    public:

        static std::shared_ptr<AppSharedInfoReader> Make(const std::string& shm_name);

        explicit AppSharedInfoReader(const std::string& shm_name);

        // write data to target shared memory
        std::shared_ptr<AppSharedMessage> ReadData();
        void Exit();

    private:
        void GuaranteeTargetMemory(const std::string& shm_name);

    private:

        //std::shared_ptr<Poco::SharedMemory> target_memory_;

    };

}

#endif //TC_APPLICATION_APP_SHARED_INFO_H
