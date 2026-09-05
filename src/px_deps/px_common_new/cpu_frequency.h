//
// Created by RGAA on 20/09/2025.
//

#ifndef GAMMARAYPREMIUM_CPU_FREQUENCY_H
#define GAMMARAYPREMIUM_CPU_FREQUENCY_H

#ifdef WIN32
#include <Pdh.h>
#include <PdhMsg.h>
#pragma comment(lib, "pdh.lib")
#endif

namespace px {

    class CpuFrequency {
    public:
        static double GetCurrentCpuSpeed();
    };

}  // namespace px

#endif  // GAMMARAYPREMIUM_CPU_FREQUENCY_H
