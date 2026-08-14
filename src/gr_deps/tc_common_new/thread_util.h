//
// Created by RGAA on 7/03/2025.
//

#ifndef GAMMARAY_THREAD_UTIL_H
#define GAMMARAY_THREAD_UTIL_H

#include <thread>

namespace tc
{

    static unsigned int GetCurrentThreadID() {
        auto tid = std::this_thread::get_id();
        auto id = *(unsigned int*)&tid;
        return id;

    }

}

#endif //GAMMARAY_THREAD_UTIL_H
