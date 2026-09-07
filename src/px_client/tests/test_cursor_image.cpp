#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "px_client/cursor_image.h"

namespace px {
TEST(CursorImageTest, PhysicalSizeDoesNotGrowAcrossRepeatedDpiCaptures) {
    for (const qreal ratio : {1.0, 1.25, 1.5, 2.0, 3.0}) {
        auto physical_size = QSize(48, 48);
        for (int iteration = 0; iteration < 64; ++iteration) {
            const auto bitmap = std::vector<char>(physical_size.width() * physical_size.height() * 4, '\x7f');
            const auto cursor = MakeCursorImage(bitmap, physical_size.width(), physical_size.height(), 6, 6, ratio);
            ASSERT_TRUE(cursor.has_value());
            EXPECT_EQ(cursor->image.devicePixelRatio(), ratio);
            EXPECT_EQ(cursor->logical_hotspot, QPoint(qRound(6 / ratio), qRound(6 / ratio)));
            // Same conversion as Qt's Windows cursor adapter: target DPR / pixmap DPR.
            physical_size = (QSizeF(cursor->image.size()) * (ratio / cursor->image.devicePixelRatio())).toSize();
            EXPECT_EQ(physical_size, QSize(48, 48));
        }
    }
}

TEST(CursorImageTest, OwnsPixelsAfterIncomingMessageIsDestroyed) {
    std::optional<CursorImage> cursor{};
    {
        const auto bitmap = std::vector<char>(16, '\x7f');
        cursor = MakeCursorImage(bitmap, 2, 2, 1, 1, 1.5);
    }
    ASSERT_TRUE(cursor.has_value());
    EXPECT_EQ(cursor->image.pixelColor(1, 1), QColor(127, 127, 127, 127));
}

TEST(CursorImageTest, RejectsInvalidGeometryPayloadAndHotspot) {
    const auto bitmap = std::vector<char>(16, '\x7f');
    EXPECT_FALSE(MakeCursorImage(bitmap, 0, 2, 0, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, 2, 0, 0, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, std::numeric_limits<uint32_t>::max(), 2, 0, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, 2, 3, 0, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, 1, 1, 0, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, 2, 2, 2, 0, 1));
    EXPECT_FALSE(MakeCursorImage(bitmap, 2, 2, 0, 2, 1));
    EXPECT_FALSE(MakeCursorImage({}, 2, 2, 0, 0, 1));
}

TEST(CursorImageTest, RejectsInvalidDpiAndConvertsForEachTargetScreen) {
    const auto bitmap = std::vector<char>(16, '\x7f');
    for (const qreal ratio :
         {0.0, -1.0, std::numeric_limits<qreal>::infinity(), std::numeric_limits<qreal>::quiet_NaN(), std::numeric_limits<qreal>::min()}) {
        EXPECT_FALSE(MakeCursorImage(bitmap, 2, 2, 1, 1, ratio));
    }
    const auto first_screen = MakeCursorImage(bitmap, 2, 2, 1, 1, 1);
    const auto second_screen = MakeCursorImage(bitmap, 2, 2, 1, 1, 2);
    ASSERT_TRUE(first_screen.has_value());
    ASSERT_TRUE(second_screen.has_value());
    EXPECT_EQ(first_screen->image.size(), second_screen->image.size());
    EXPECT_NE(first_screen->image.devicePixelRatio(), second_screen->image.devicePixelRatio());
}
} // namespace px
