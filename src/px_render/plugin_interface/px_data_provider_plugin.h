//
// Created by RGAA on 26/11/2024.
//

#ifndef PX_DATA_PROVIDER_PLUGIN_H
#define PX_DATA_PROVIDER_PLUGIN_H

#include "px_plugin_interface.h"

namespace px
{

    class PxDataProviderPlugin : public PxPluginInterface {
    public:
        PxDataProviderPlugin();
        ~PxDataProviderPlugin() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;
        virtual void StartProviding();
        virtual void StopProviding();

        // Game-hook: host-side WASAPI process-loopback target PID (0 = desktop default mix).
        virtual void SetAudioLoopbackProcessId(uint32_t pid) { (void)pid; }
        virtual uint32_t GetAudioLoopbackProcessId() const { return 0; }
        // True after a successful StartProviding() while capture object is alive.
        virtual bool IsProviding() const { return false; }
        // 0 = ok / not started; non-zero = last StartProviding backend error (e.g. ma_result).
        virtual int GetLastStartError() const { return 0; }
    };

}

#endif //PX_DATA_PROVIDER_PLUGIN_H
