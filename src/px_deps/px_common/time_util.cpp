//
// Created by RGAA on 2023-12-17.
//

#include "time_util.h"
#include "log.h"

#include <utility>

namespace px
{

    TimeDuration::TimeDuration(std::string name) : begin_time_(std::chrono::steady_clock::now()), name_(std::move(name)) {}

    TimeDuration::~TimeDuration() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin_time_);
        LOGI("[{}] used {}ms", name_, elapsed.count());
    }

}
