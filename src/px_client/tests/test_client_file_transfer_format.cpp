#include "client_file_transfer_format.h"

#include <limits>

#include <gtest/gtest.h>

namespace px::client::imgui {
namespace {

TEST(ClientFileTransferFormat, SelectsReadableBinaryUnit) {
    EXPECT_EQ(FormatTransferSpeed(0.0), "0 B/s");
    EXPECT_EQ(FormatTransferSpeed(512.0), "512 B/s");
    EXPECT_EQ(FormatTransferSpeed(1024.0), "1.0 KB/s");
    EXPECT_EQ(FormatTransferSpeed(1024.0 * 1024.0), "1.0 MB/s");
    EXPECT_EQ(FormatTransferSpeed(12578.2 * 1024.0), "12.3 MB/s");
    EXPECT_EQ(FormatTransferSpeed(1024.0 * 1024.0 * 1024.0), "1.0 GB/s");
}

TEST(ClientFileTransferFormat, SanitizesInvalidSpeed) {
    EXPECT_EQ(FormatTransferSpeed(-1.0), "0 B/s");
    EXPECT_EQ(FormatTransferSpeed(std::numeric_limits<double>::infinity()), "0 B/s");
    EXPECT_EQ(FormatTransferSpeed(std::numeric_limits<double>::quiet_NaN()), "0 B/s");
}

} // namespace
} // namespace px::client::imgui
