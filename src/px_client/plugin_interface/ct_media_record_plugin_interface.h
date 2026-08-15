#pragma once

#include "ct_plugin_interface.h"

namespace px
{

    class MediaRecordPluginClientInterface : public ClientPluginInterface {
    public:
        virtual void StartRecord() {};
        virtual void EndRecord() {};
    };

}