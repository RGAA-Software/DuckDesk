#include <gtest/gtest.h>

#include <utility>

#include "ingress/application_clipboard.h"

namespace {
struct ClipboardState final {
    std::string text{};
    bool busy{};
    int writes{};
};

class TestClipboardPlatform final : public px::clipboard::IPlatform {
public:
    explicit TestClipboardPlatform(std::shared_ptr<ClipboardState> state) : state_{std::move(state)} {}
    bool Read(px::clipboard::Content& content) override {
        if (state_->busy) return false;
        content.text_ = state_->text;
        return true;
    }
    bool WriteText(const std::string& text) override {
        if (state_->busy) return false;
        ++state_->writes;
        state_->text = text;
        return true;
    }
    bool Clear() override { return false; }

private:
    std::shared_ptr<ClipboardState> state_{};
};

TEST(ApplicationClipboard, RemoteWriteIsNotEchoedButLocalEditAndReconnectAreSent) {
    const auto state = std::make_shared<ClipboardState>();
    px::ApplicationClipboard clipboard{std::make_unique<TestClipboardPlatform>(state)};
    ASSERT_TRUE(clipboard.WriteRemoteText("remote text"));
    EXPECT_FALSE(clipboard.ReadChangedText());
    state->text = "local edit";
    EXPECT_EQ(clipboard.ReadChangedText(), "local edit");
    EXPECT_FALSE(clipboard.ReadChangedText());
    clipboard.Refresh();
    EXPECT_EQ(clipboard.ReadChangedText(), "local edit");
}

TEST(ApplicationClipboard, BusyClipboardDoesNotAcknowledgeOrConsumeAnEdit) {
    const auto state = std::make_shared<ClipboardState>();
    px::ApplicationClipboard clipboard{std::make_unique<TestClipboardPlatform>(state)};
    state->busy = true;
    EXPECT_FALSE(clipboard.WriteRemoteText("remote"));
    state->text = "local pending";
    EXPECT_FALSE(clipboard.ReadChangedText());
    state->busy = false;
    EXPECT_EQ(clipboard.ReadChangedText(), "local pending");
    EXPECT_TRUE(clipboard.WriteRemoteText("remote"));
    EXPECT_FALSE(clipboard.ReadChangedText());
}

TEST(ApplicationClipboard, BoundsAndRepeatedLifetimesPreserveSystemClipboard) {
    const auto state = std::make_shared<ClipboardState>();
    state->text = "retained";
    for (int lifecycle{}; lifecycle < 3; ++lifecycle) {
        px::ApplicationClipboard clipboard{std::make_unique<TestClipboardPlatform>(state)};
        EXPECT_FALSE(clipboard.WriteRemoteText(""));
        EXPECT_FALSE(clipboard.WriteRemoteText(std::string(1024U * 1024U + 1U, 'z')));
        EXPECT_EQ(clipboard.ReadChangedText(), "retained");
    }
    EXPECT_EQ(state->text, "retained");
    EXPECT_EQ(state->writes, 0);
}
}  // namespace
