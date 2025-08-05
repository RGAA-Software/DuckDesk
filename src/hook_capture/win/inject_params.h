//
// Created by RGAA on 2023-12-20.
//

#ifndef TC_APPLICATION_INJECT_PARAMS_H
#define TC_APPLICATION_INJECT_PARAMS_H

#include <cstdint>

namespace tc
{
    // InjectParams
    struct InjectParams {
        char host_exe_folder[512] = {0};
        uint32_t enable_hook_audio = 0;
        uint32_t enable_gpu_router = 0;
        uint64_t gpu_router_target_lid = 0;
        uint64_t gpu_router_target_hid = 0;
        uint32_t listening_port = 0;
        uint32_t shm_client_to_host_buffer_size = 0;
        uint32_t send_video_frame_by_shm = 0;
    };
}


#endif //TC_APPLICATION_INJECT_PARAMS_H
