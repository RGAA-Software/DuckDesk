//
// Created by RGAA on 2023/12/19.
//

#include <gtest/gtest.h>
#include <libyuv.h>
#include <fstream>

#include "../video_encoder_factory.h"
#include "../ffmpeg_video_encoder.h"
#include "../video_encoder.h"
#include "tc_common_new/image.h"
#include "tc_common_new/data.h"
#include "tc_common_new/file.h"
#include "tc_common_new/log.h"

using namespace tc;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}

TEST(Test_FFmpeg, encoder) {
    auto file = File::OpenForReadB("test.png");
    ASSERT_EQ(file->Exists(), true);
    ASSERT_GT(file->Size(), 0);
    auto data = file->ReadAll();
    auto image = Image::MakeByCompressedImage(data);
    LOGI("image info: {} x {} , channel: {}, size: {} ", image->width, image->height, image->channels, image->data->Size());

    auto params = VideoEncoderParams {
        .width_ = image->width,
        .height_ = image->height,
        .format_ = VideoEncoderFormat::kHEVC,
    };
    auto encoder = VideoEncoderFactory::CreateEncoder(nullptr, EncoderFeature{}, ECreateEncoderPolicy::kAuto, EncoderConfig{});
    ASSERT_TRUE(encoder != nullptr);

    //
    size_t y_pixel_size = image->width * image->height;

    uint8_t* yuv_frame_data = nullptr;
    const int uv_stride = image->width/2;
    int yuv_frame_size = image->width * image->height * 1.5;
    LOGI("yuv_frame_size: {}", yuv_frame_size);
    yuv_frame_data = (uint8_t*)malloc(yuv_frame_size);

    auto y = (uint8_t*)yuv_frame_data;
    uint8_t* u = y + y_pixel_size;
    uint8_t* v = y + y_pixel_size + y_pixel_size/4;

//    auto yb = (uint8_t*)malloc(y_pixel_size);
//    auto ub = (uint8_t*)malloc(y_pixel_size/4);
//    auto vb = (uint8_t*)malloc(y_pixel_size/4);

    int ret = libyuv::ABGRToI420((uint8_t*)image->data->DataAddr(), image->width*4, y, image->width, u, uv_stride, v, uv_stride, image->width, image->height);
    ASSERT_TRUE(ret == 0);
    std::ofstream encoded_file("test_ffmpeg.h265", std::ios::binary);
    encoder->RegisterEncodeCallback([&](const std::shared_ptr<Image>& frame, uint64_t frame_index, bool key) {
        LOGI("encoder callback : {}, buffer size: {}", frame_index, frame->data->Size());
        encoded_file.write(frame->data->DataAddr(), frame->data->Size());
    });
    for (int i = 0; i < 100; i++) {
        auto data = Data::Make((char*)yuv_frame_data, yuv_frame_size);
        auto input = Image::Make(data, image->width, image->height, 0);
        encoder->Encode(input, i);
    }
    encoded_file.close();
}