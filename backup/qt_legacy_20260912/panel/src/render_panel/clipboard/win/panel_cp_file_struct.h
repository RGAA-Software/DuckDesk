//
// Created by RGAA on 8/04/2025.
//

#ifndef PX_PANEL_CP_FILE_STRUCT_H
#define PX_PANEL_CP_FILE_STRUCT_H

#include <QString>
#include <cstdint>
#include "px_message.pb.h"

namespace px
{

    class ClipboardFileWrapper {
    public:
        std::string device_id_;
        std::string stream_id_;
        ClipboardFile file_;
    };

}

#endif //PX_PANEL_CP_FILE_STRUCT_H
