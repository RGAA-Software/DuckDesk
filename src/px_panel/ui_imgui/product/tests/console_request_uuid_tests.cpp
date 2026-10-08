#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "px_common/uuid.h"
#include "px_console_client/console_resource_api.h"
#include "px_console_client/console_user_app_api.h"

namespace px::panel::product {

TEST(ConsoleRequestUuid, CanonicalGeneratorProducesUniqueVersionFourProtocolIdentifiers) {
    const std::regex uuid_pattern{"[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}"};
    std::unordered_set<std::string> request_ids{};
    for (std::size_t request_index{}; request_index < 128; ++request_index) {
        const std::string request_id{GetCanonicalUUID()};
        EXPECT_TRUE(std::regex_match(request_id, uuid_pattern));
        EXPECT_TRUE(IsCanonicalUUID(request_id));
        EXPECT_TRUE(request_ids.insert(request_id).second);
    }
}

TEST(ConsoleRequestUuid, RandomBase64IdentifiersKeepTheirDistinctOpaqueContract) {
    constexpr std::string_view alphabet{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
    const std::string opaque_identifier{GenerateRandomBase64Id()};
    EXPECT_EQ(opaque_identifier.size(), 32U);
    EXPECT_TRUE(
        std::ranges::all_of(opaque_identifier, [alphabet](const char character) { return alphabet.find(character) != std::string_view::npos; }));
    EXPECT_FALSE(IsCanonicalUUID(opaque_identifier));
    const std::regex md5_pattern{"[0-9a-f]{32}"};
    EXPECT_TRUE(std::regex_match(GenerateRandomBase64IdMd5(), md5_pattern));
}

TEST(ConsoleRequestUuid, ValidationRejectsMalformedIdentifiersWithoutRelaxingUuidFormat) {
    EXPECT_TRUE(IsCanonicalUUID("93488646-70a1-42b3-9c4d-0123456789ab"));
    EXPECT_TRUE(IsCanonicalUUID("93488646-70A1-42B3-9C4D-0123456789AB"));
    const std::array<std::string_view, 7> invalid_identifiers{"",
                                                              "934886467",
                                                              "9348864670a142b39c4d0123456789ab00",
                                                              "93488646/70a1-42b3-9c4d-0123456789ab",
                                                              "93488646-70a1-42b3-9c4d-0123456789aZ",
                                                              "93488646-70a1-42b3-9c4d-0123456789ab/",
                                                              " 93488646-70a1-42b3-9c4d-0123456789ab"};
    for (const auto& identifier : invalid_identifiers) {
        EXPECT_FALSE(IsCanonicalUUID(identifier)) << identifier;
    }
}

TEST(ConsoleRequestUuid, ApplicationStartRejectsOpaqueRequestIdsBeforeNetworkDispatch) {
    const std::string request_id{GenerateRandomBase64Id()};
    for (const bool guest : {false, true}) {
        const auto result = px_console::ConsoleUserAppApi::StartApp("", 0, {}, "8ebdbd8e-925d-4885-9401-e87d0418bff9", request_id, guest);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), px_console::ConsoleApiError::kInvalidParams);
    }
}

TEST(ConsoleRequestUuid, DesktopAndApplicationSessionsRejectOpaqueRequestIdsBeforeNetworkDispatch) {
    const std::array<px_console::ConsoleResourceTarget, 2> targets{
        px_console::ConsoleResourceTarget{.kind = px_console::ConsoleResourceTargetKind::Desktop,
                                          .device_id = "b8298e0b-712d-490e-9936-29f6c07333b2"},
        px_console::ConsoleResourceTarget{.kind = px_console::ConsoleResourceTargetKind::CloudApplication,
                                          .application_id = "8ebdbd8e-925d-4885-9401-e87d0418bff9",
                                          .instance_id = "93488646-70a1-42b3-9c4d-0123456789ab"}};
    for (const auto& target : targets) {
        for (const bool guest : {false, true}) {
            for (const bool view_only : {false, true}) {
                const auto result = px_console::OpenPanelResourceConnection("", 0, {}, guest, target, view_only, GenerateRandomBase64Id());
                ASSERT_FALSE(result.has_value());
                EXPECT_EQ(result.error(), px_console::ConsoleApiError::kInvalidParams);
            }
        }
    }
}

}  // namespace px::panel::product
