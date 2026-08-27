//
// Created by RGAA on 23/05/2025.
//

#ifndef PX_CT_PLUGIN_IDS_H
#define PX_CT_PLUGIN_IDS_H

#include <string>

namespace px
{

    const std::string kClientMediaRecordPluginId = "db49cbd4-8800-4746-ae3d-efd89801d33f";
    const std::string kClientClipboardPluginId = "113ca5af-f31f-4868-9b8b-ac436e4a3531";
    // rustdesk 协议迁移阶段 3:新文件传输插件(rustdesk 语义 + px_ft_engine)
    const std::string kClientFtPluginId = "9f4e2c7a-3b5d-4e8f-a1c6-7d9b2e4f5a8c";

}

#endif //PX_CT_PLUGIN_IDS_H
