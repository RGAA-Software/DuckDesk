//
// Created by RGAA on 2025.
//

#include <gtest/gtest.h>
#include "../qrcode/qr_generator.h"

using namespace tc;

TEST(QRGeneratorTest, ImageSizeCorrect) {
    auto img = QrGenerator::GenQRImage("test", -1);
    EXPECT_GT(img.width, 0) << "QR width should be positive";
    EXPECT_GT(img.height, 0) << "QR height should be positive";
    EXPECT_EQ(img.width, img.height) << "QR code should be square";
    EXPECT_EQ(img.rgba.size(), static_cast<size_t>(img.width * img.height * 4))
        << "RGBA buffer size should match dimensions";
}

TEST(QRGeneratorTest, PixelsNotEmpty) {
    auto img = QrGenerator::GenQRImage("hello world", -1);
    EXPECT_FALSE(img.rgba.empty()) << "RGBA buffer should not be empty";

    bool has_black = false;
    bool has_white = false;
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] == 0 && img.rgba[i+1] == 0 && img.rgba[i+2] == 0) {
            has_black = true;
        }
        if (img.rgba[i] == 255 && img.rgba[i+1] == 255 && img.rgba[i+2] == 255) {
            has_white = true;
        }
    }
    EXPECT_TRUE(has_black) << "QR image should contain black pixels";
    EXPECT_TRUE(has_white) << "QR image should contain white pixels";
}

TEST(QRGeneratorTest, DifferentMessagesProduceDifferentResults) {
    auto img1 = QrGenerator::GenQRImage("message1", -1);
    auto img2 = QrGenerator::GenQRImage("message2", -1);

    EXPECT_EQ(img1.width, img2.width) << "Same length messages should produce same size QR";
    EXPECT_NE(img1.rgba, img2.rgba) << "Different messages should produce different pixel data";
}

TEST(QRGeneratorTest, ScaleSizeCorrect) {
    auto img = QrGenerator::GenQRImage("scale test", 256);
    EXPECT_EQ(img.width, 256) << "Scaled width should match requested size";
    EXPECT_EQ(img.height, 256) << "Scaled height should match requested size";
    EXPECT_EQ(img.rgba.size(), static_cast<size_t>(256) * 256 * 4) << "Scaled RGBA buffer size should match";
}
