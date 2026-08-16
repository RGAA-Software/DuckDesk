//
// Created by RGAA on 26/11/2024.
//

#include "px_data_provider_plugin.h"

namespace px
{

    PxDataProviderPlugin::PxDataProviderPlugin() {

    }

    PxDataProviderPlugin::~PxDataProviderPlugin() {

    }

    bool PxDataProviderPlugin::OnCreate(const px::PxPluginParam& param) {
        PxPluginInterface::OnCreate(param);
        return true;
    }

    bool PxDataProviderPlugin::OnDestroy() {
        return PxPluginInterface::OnDestroy();
    }

    void PxDataProviderPlugin::StartProviding() {

    }

    void PxDataProviderPlugin::StopProviding() {

    }

}
