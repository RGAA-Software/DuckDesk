//
// Created by RGAA on 20/09/2025.
//

#include "cpu_frequency.h"

namespace tc
{

    double CpuFrequency::GetCurrentCpuSpeed() {
#ifdef WIN32
        HQUERY query;
        //打开PDH
        PDH_STATUS status = PdhOpenQuery(NULL, NULL, &query);
        if (status != ERROR_SUCCESS)
            return -1;

        HCOUNTER cpuPerformance;
        HCOUNTER cpuBasicSpeed;

        //添加CPU当前性能的计数器
        status = PdhAddCounter(query, TEXT("\\Processor Information(_Total)\\% Processor Performance"), NULL, &cpuPerformance);
        if (status != ERROR_SUCCESS)
            return -1;

        //添加CPU基准频率的计数器
        status = PdhAddCounter(query, TEXT("\\Processor Information(_Total)\\Processor Frequency"), NULL, &cpuBasicSpeed);
        if (status != ERROR_SUCCESS)
            return -1;

        //收集计数 因很多计数需要区间值 所以需要调用两次Query(间隔至少1s) 然后再获取计数值
        PdhCollectQueryData(query);
        Sleep(50);
        PdhCollectQueryData(query);

        PDH_FMT_COUNTERVALUE pdhValue;
        DWORD dwValue;

        status = PdhGetFormattedCounterValue(cpuPerformance, PDH_FMT_DOUBLE, &dwValue, &pdhValue);
        if (status != ERROR_SUCCESS)
            return -1;
        double cpu_performance = pdhValue.doubleValue / 100.0;

        status = PdhGetFormattedCounterValue(cpuBasicSpeed, PDH_FMT_DOUBLE, &dwValue, &pdhValue);
        if (status != ERROR_SUCCESS)
            return -1;
        double basic_speed = pdhValue.doubleValue;

        //关闭PDH
        PdhCloseQuery(query);

        return (double)(cpu_performance * basic_speed / 1000.0);
#else
       return 0;
#endif
    }

}