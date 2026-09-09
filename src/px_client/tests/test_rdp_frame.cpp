#include "rdp/rdp_frame.h"
#include <gtest/gtest.h>

namespace px::rdp {

TEST(RdpFrame, PackedRectanglesMustMatchExactlyBeforeUpload) {
    DesktopFrame frame{1, 0, {100, 100}, {{0, 0, 10, 10}, {20, 20, 1, 1}}, QByteArray(404, '\0')};
    EXPECT_TRUE(frame.IsValid());
    frame.pixels.append('x');
    EXPECT_FALSE(frame.IsValid());
    frame.pixels.chop(2);
    EXPECT_FALSE(frame.IsValid());
    frame.pixels.append('x');
    frame.rectangles[1] = {-1, 0, 1, 1};
    EXPECT_FALSE(frame.IsValid());
    frame.rectangles[1] = {100, 0, 1, 1};
    EXPECT_FALSE(frame.IsValid());
    frame.rectangles[1] = {99, 99, 1, 1};
    EXPECT_TRUE(frame.IsValid());
    frame.desktop = {8192, 8192};
    EXPECT_FALSE(frame.IsValid());
}

TEST(RdpFrame, ScaledLetterboxedAndHighDpiCoordinates) {
    EXPECT_EQ(DesktopViewport({1920, 1080}, {1280, 800}), QRect(0, 40, 1280, 720));
    EXPECT_EQ(MapDesktopPoint({1920, 1080}, {1280, 800}, {640, 400}), QPoint(960, 540));
    EXPECT_EQ(MapDesktopPoint({3840, 2160}, {1280, 800}, {640, 400}), QPoint(1920, 1080));
    EXPECT_FALSE(MapDesktopPoint({1920, 1080}, {1280, 800}, {640, 39}));
    EXPECT_EQ(MapDesktopPoint({1920, 1080}, {1280, 800}, {3000, -10}, true), QPoint(1919, 0));
    EXPECT_FALSE(MapDesktopPoint({}, {1280, 800}, {0, 0}));
    EXPECT_FALSE(MapDesktopPoint({1920, 1080}, {}, {0, 0}));
}

} // namespace px::rdp
