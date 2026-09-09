#include <gtest/gtest.h>

#include <array>
#include <string>

#include "application_text_validation.h"
#include "px_message.pb.h"
#include "message_type_ids.h"

namespace px {

static_assert(static_cast<int>(wire::kApplicationTextCapabilities) == kApplicationTextCapabilities);
static_assert(static_cast<int>(wire::kApplicationTextState) == kApplicationTextState);
static_assert(static_cast<int>(wire::kApplicationTextSubmit) == kApplicationTextSubmit);
static_assert(static_cast<int>(wire::kApplicationTextResult) == kApplicationTextResult);
static_assert(static_cast<int>(wire::kApplicationTextBarrier) == kApplicationTextBarrier);
static_assert(static_cast<int>(wire::kApplicationTextBarrierResult) == kApplicationTextBarrierResult);

TEST(ApplicationTextValidation, PreservesWhitespaceAndCountsBytes) {
    EXPECT_TRUE(ValidApplicationText(" \t\r\n", 4));
    EXPECT_TRUE(ValidApplicationText("\xe4\xb8\xad", 3));
    EXPECT_FALSE(ValidApplicationText("\xe4\xb8\xad", 2));
    EXPECT_TRUE(ValidApplicationText("\xf0\x9f\x98\x80", 4));
    EXPECT_TRUE(ValidApplicationText(std::string(16384, 'a')));
    EXPECT_FALSE(ValidApplicationText(std::string(16385, 'a')));
}

TEST(ApplicationTextValidation, RejectsInvalidEncodingAndControls) {
    const std::array<std::string_view, 9> invalid{"", "\x01", "\x7f", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe4\xb8", "\xe4x"};
    for (const auto text : invalid) {
        EXPECT_FALSE(ValidApplicationText(text));
    }
    EXPECT_FALSE(ValidApplicationText(std::string(1, '\0')));
    EXPECT_FALSE(ValidApplicationText("a", 0));
    EXPECT_FALSE(ValidApplicationText("a", 16385));
}

TEST(ApplicationTextValidation, BoundsRequestIdentity) {
    EXPECT_TRUE(ValidApplicationTextRequestId("a_B-09"));
    EXPECT_FALSE(ValidApplicationTextRequestId(""));
    EXPECT_FALSE(ValidApplicationTextRequestId("a b"));
    EXPECT_FALSE(ValidApplicationTextRequestId(std::string(65, 'a')));
}

TEST(ApplicationTextProtocol, KeepsLegacyAndNewSemanticsSeparate) {
    EXPECT_EQ(kTextInput, 580);
    EXPECT_EQ(kApplicationTextSubmit, 612);
    auto message{Message{}};
    message.set_type(kApplicationTextSubmit);
    auto& submit{*message.mutable_application_text_submit()};
    submit.set_request_id("request-1");
    submit.set_text("\xe4\xb8\xad\xf0\x9f\x98\x80\n");
    submit.set_input_generation("7");
    auto& target{*submit.mutable_target()};
    target.set_instance_id("app-a");
    target.set_lease_generation("9007199254740993");
    target.set_target_generation("2");
    const auto wire{message.SerializeAsString()};
    const std::string_view golden{
        "50e404a226360a09726571756573742d31121c0a056170702d611210393030373139393235343734303939331a01321a08e4b8adf09f98800a220137"};
    std::string encoded_hex{};
    for (const auto character : wire) {
        const auto byte{static_cast<unsigned char>(character)};
        const std::string_view digits{"0123456789abcdef"};
        encoded_hex += digits[byte >> 4];
        encoded_hex += digits[byte & 15];
    }
    EXPECT_EQ(encoded_hex, golden);
    auto decoded{Message{}};
    ASSERT_TRUE(decoded.ParseFromString(wire));
    EXPECT_FALSE(decoded.has_text_input());
    EXPECT_EQ(decoded.application_text_submit().target().lease_generation(), "9007199254740993");
    EXPECT_EQ(decoded.application_text_submit().text(), submit.text());
    EXPECT_EQ(decoded.application_text_submit().input_generation(), "7");
}

} // namespace px
