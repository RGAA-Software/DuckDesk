//
// Created by RGAA on 2024/3/17.
//

#include "app_shared_info_reader.h"
#include "tc_common_new/log.h"

constexpr auto kAppSharedInfoReaderBuffSize = 1024 * 4;

namespace tc
{

    std::shared_ptr<AppSharedInfoReader> AppSharedInfoReader::Make(const std::string& shm_name) {
        return std::make_shared<AppSharedInfoReader>(shm_name);
    }

    AppSharedInfoReader::AppSharedInfoReader(const std::string& shm_name) {
        //target_memory_ = std::make_shared<Poco::SharedMemory>(shm_name, kAppSharedInfoReaderBuffSize, Poco::SharedMemory::AccessMode::AM_READ);
    }

    std::shared_ptr<AppSharedMessage> AppSharedInfoReader::ReadData() {
//        auto shm_msg = std::make_shared<AppSharedMessage>();
//        memcpy(shm_msg.get(), target_memory_->begin(), sizeof(AppSharedMessage));
//        return shm_msg;
        return nullptr;
    }

    void AppSharedInfoReader::Exit() {

    }

}