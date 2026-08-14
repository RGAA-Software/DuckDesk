//
// Created by RGAA on 2024/4/8.
//

#include "qr_generator.h"
#include "qrcodegen.hpp"
#include "tc_common_new/log.h"

using namespace qrcodegen;

namespace tc
{

    QRImage QrGenerator::GenQRImage(const std::string& message, int qr_size) {
        std::vector<QrSegment> segs = QrSegment::makeSegments(message.c_str());
        QrCode qr1 = QrCode::encodeSegments(segs, QrCode::Ecc::LOW, 5, 15, 1, false);
        int qr_pixel_size = qr1.getSize();
        LOGI("QRCode, size: {}", qr_pixel_size);

        QRImage image;
        image.width = qr_pixel_size;
        image.height = qr_pixel_size;
        image.rgba.resize(qr_pixel_size * qr_pixel_size * 4);

        for (int y = 0; y < qr_pixel_size; y++) {
            for (int x = 0; x < qr_pixel_size; x++) {
                size_t idx = static_cast<size_t>((y * qr_pixel_size + x) * 4);
                if (qr1.getModule(x, y)) {
                    image.rgba[idx + 0] = 0;
                    image.rgba[idx + 1] = 0;
                    image.rgba[idx + 2] = 0;
                    image.rgba[idx + 3] = 255;
                } else {
                    image.rgba[idx + 0] = 255;
                    image.rgba[idx + 1] = 255;
                    image.rgba[idx + 2] = 255;
                    image.rgba[idx + 3] = 255;
                }
            }
        }

        if (qr_size != -1 && qr_size != qr_pixel_size) {
            QRImage scaled;
            scaled.width = qr_size;
            scaled.height = qr_size;
            scaled.rgba.resize(static_cast<size_t>(qr_size) * qr_size * 4);

            for (int y = 0; y < qr_size; y++) {
                for (int x = 0; x < qr_size; x++) {
                    int src_x = x * qr_pixel_size / qr_size;
                    int src_y = y * qr_pixel_size / qr_size;
                    size_t src_idx = static_cast<size_t>((src_y * qr_pixel_size + src_x) * 4);
                    size_t dst_idx = static_cast<size_t>((y * qr_size + x) * 4);
                    scaled.rgba[dst_idx + 0] = image.rgba[src_idx + 0];
                    scaled.rgba[dst_idx + 1] = image.rgba[src_idx + 1];
                    scaled.rgba[dst_idx + 2] = image.rgba[src_idx + 2];
                    scaled.rgba[dst_idx + 3] = image.rgba[src_idx + 3];
                }
            }
            return scaled;
        }

        return image;
    }

}
