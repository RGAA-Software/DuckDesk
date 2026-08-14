//
// Created by RGAA on 2024/4/8.
//

#ifndef TC_SERVER_STEAM_QR_GENERATOR_H
#define TC_SERVER_STEAM_QR_GENERATOR_H

#include <cstdint>
#include <vector>
#include <string>

namespace tc
{

    struct QRImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    class QrGenerator {
    public:
        static QRImage GenQRImage(const std::string& message, int qr_size);
    };

}

#endif //TC_SERVER_STEAM_QR_GENERATOR_H
