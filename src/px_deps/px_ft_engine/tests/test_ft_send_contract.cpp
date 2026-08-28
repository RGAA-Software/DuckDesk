#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "ft_engine.h"

namespace px::ft {
namespace {

struct SendProbe {
    int attempts = 0;
    std::vector<px::Message> accepted_messages;
};

TEST(FtSendContract, BusyMessageIsAcceptedExactlyOnceOnRetry) {
    const auto probe = std::make_shared<SendProbe>();
    auto engine = std::make_shared<FtEngine>([probe](const px::Message& message) {
        ++probe->attempts;
        if (probe->attempts == 1) {
            return false;
        }
        probe->accepted_messages.push_back(message);
        return true;
    });

    engine->ReceiveFiles("remote-file.bin", false, "local-file.bin");
    ASSERT_EQ(probe->attempts, 1);
    ASSERT_TRUE(probe->accepted_messages.empty());

    engine->Tick();

    ASSERT_EQ(probe->attempts, 2);
    ASSERT_EQ(probe->accepted_messages.size(), 1U);
    EXPECT_TRUE(probe->accepted_messages.front().has_file_action());
    EXPECT_TRUE(probe->accepted_messages.front().file_action().has_send());

    engine->Tick();
    EXPECT_EQ(probe->attempts, 2);
    EXPECT_EQ(probe->accepted_messages.size(), 1U);
}

} // namespace
} // namespace px::ft
