//
// Created by RGAA on 18/09/2025.
//

#include <gtest/gtest.h>

#include "../uuid.h"
#include "../data.h"
#include "../md5.h"
#include "../random.h"
#include "../task_runtime.h"
#include "../log.h"
#include "../base64.h"
#include "../file.h"
#include "../tc_aes.h"
#include "../http_client.h"
#include "../cpu_frequency.h"

using namespace tc;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}

TEST(Test_Cpu, Cpu_frequency) {
    for (int i = 0; i < 10; i++) {
        auto freq = CpuFrequency::GetCurrentCpuSpeed();
        LOGI("freq: {} - {}", freq, freq/1024);
    }
}