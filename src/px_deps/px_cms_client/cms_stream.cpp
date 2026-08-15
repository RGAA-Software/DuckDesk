//
// Created by RGAA on 1/11/2025.
//

#include "cms_stream.h"

namespace px_cms
{

    bool CmsStream::IsValid() const {
        return !stream_id_.empty();
    }

    bool CmsStream::HasRelayInfo() const {
        return !remote_device_id_.empty() && !relay_host_.empty() && relay_port_ > 0;
    }

}