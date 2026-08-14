//
// Created by RGAA on 6/06/2025.
//

#ifndef GAMMARAY_CLIENT_ID_EXTRACTOR_H
#define GAMMARAY_CLIENT_ID_EXTRACTOR_H

#include <vector>
#include "string_util.h"

namespace tc
{

    // client_702789003_d41d8cd98f00b204e9800998ecf8427e
    // ==> 702789003
    static std::string ExtractClientId(const std::string& full_id) {
        std::vector<std::string> result;
        StringUtil::Split(full_id, result, "_");
        if (result.size() > 1) {
            return result[1];
        }
        return full_id;
    }

}

#endif //GAMMARAY_CLIENT_ID_EXTRACTOR_H
