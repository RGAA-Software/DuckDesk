//
// Created by RGAA on 20/09/2025.
//

#ifndef GAMMARAYPREMIUM_CPU_FREQUENCY_H
#define GAMMARAYPREMIUM_CPU_FREQUENCY_H

#ifdef WIN32
#include <Pdh.h>
#include <PdhMsg.h>
#include <iostream>
#pragma comment(lib,"pdh.lib")
#endif

namespace tc
{

    class CpuFrequency {
    public:
        static double GetCurrentCpuSpeed();
    };

}

#endif //GAMMARAYPREMIUM_CPU_FREQUENCY_H
