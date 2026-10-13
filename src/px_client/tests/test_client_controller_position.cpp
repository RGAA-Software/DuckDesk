#include <Windows.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <thread>

#include "client_controller_position.h"
#include "client_ui_settings.h"
#include "px_common/shared_preference.h"
#include "px_common/win32/unique_win_handle.h"

namespace px::client::imgui {
namespace {

TEST(ControllerPosition, FirstLaunchUsesContentTopLeft) {
    const ControllerArea area{0.0F, 40.0F, 1440.0F, 860.0F, 40.0F};
    const auto position = area.Restore(std::nullopt, 14.0F);
    EXPECT_FLOAT_EQ(position.x, 14.0F);
    EXPECT_FLOAT_EQ(position.y, 54.0F);
}

TEST(ControllerPosition, DragCannotEnterTitleBarOrLeaveWindow) {
    const ControllerArea area{10.0F, 60.0F, 1000.0F, 600.0F, 40.0F};
    const auto upperLeft = area.Clamp({-200.0F, -200.0F});
    EXPECT_FLOAT_EQ(upperLeft.x, 10.0F);
    EXPECT_FLOAT_EQ(upperLeft.y, 60.0F);
    const auto lowerRight = area.Clamp({3000.0F, 3000.0F});
    EXPECT_FLOAT_EQ(lowerRight.x + area.diameter, area.left + area.width);
    EXPECT_FLOAT_EQ(lowerRight.y + area.diameter, area.top + area.height);
}

TEST(ControllerPosition, ResizeRestoresPercentagesInsteadOfPixels) {
    const ControllerArea original{0.0F, 40.0F, 1000.0F, 600.0F, 40.0F};
    const auto stored = original.Normalize({480.0F, 180.0F});
    EXPECT_FLOAT_EQ(stored.horizontalRatio, 0.5F);
    EXPECT_FLOAT_EQ(stored.verticalRatio, 0.25F);
    const ControllerArea resized{0.0F, 60.0F, 2000.0F, 1200.0F, 60.0F};
    const auto position = resized.Restore(stored, 21.0F);
    EXPECT_FLOAT_EQ(position.x, 970.0F);
    EXPECT_FLOAT_EQ(position.y, 345.0F);
}

TEST(ControllerPosition, FullscreenExitRestoresBelowTitleBarWithoutRatioDrift) {
    const ControllerArea windowed{0.0F, 40.0F, 1440.0F, 860.0F, 40.0F};
    const ControllerArea fullscreen{0.0F, 0.0F, 1920.0F, 1080.0F, 40.0F};
    const ControllerPosition stored{0.25F, 0.0F};
    EXPECT_FLOAT_EQ(fullscreen.Restore(stored, 14.0F).y, 0.0F);
    const auto restored = windowed.Restore(stored, 14.0F);
    EXPECT_FLOAT_EQ(restored.y, 40.0F);
    EXPECT_FLOAT_EQ(windowed.Normalize(restored).horizontalRatio, stored.horizontalRatio);
}

TEST(ControllerPosition, TinyWindowAvoidsInvalidClampAndDivisionByZero) {
    const ControllerArea tiny{0.0F, 40.0F, 20.0F, 0.0F, 40.0F};
    const auto position = tiny.Restore(ControllerPosition{1.0F, 1.0F}, 14.0F);
    EXPECT_FLOAT_EQ(position.x, 0.0F);
    EXPECT_FLOAT_EQ(position.y, 40.0F);
    const auto stored = tiny.Normalize(position);
    EXPECT_TRUE(stored.IsValid());
    EXPECT_FLOAT_EQ(stored.horizontalRatio, 0.0F);
    EXPECT_FLOAT_EQ(stored.verticalRatio, 0.0F);
}

TEST(ControllerPosition, InvalidRatiosFallBackToTopLeft) {
    const ControllerArea area{0.0F, 40.0F, 1000.0F, 600.0F, 40.0F};
    for (const auto invalid :
         {ControllerPosition{-0.1F, 0.5F}, ControllerPosition{0.5F, 1.1F}, ControllerPosition{std::numeric_limits<float>::quiet_NaN(), 0.5F},
          ControllerPosition{0.5F, std::numeric_limits<float>::infinity()}}) {
        EXPECT_FALSE(invalid.IsValid());
        const auto position = area.Restore(invalid, 14.0F);
        EXPECT_FLOAT_EQ(position.x, 14.0F);
        EXPECT_FLOAT_EQ(position.y, 54.0F);
    }
}

class ClientUiSettingsTest : public testing::Test {
protected:
    std::filesystem::path settingsDirectory_{std::filesystem::temp_directory_path() /
                                             ("pixels-controller-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::shared_ptr<px::SharedPreference> preferences_{std::make_shared<px::SharedPreference>()};
    ClientUiSettings settings_{preferences_};

    void SetUp() override {
        std::filesystem::create_directories(settingsDirectory_);
        ASSERT_TRUE(preferences_->Init(settingsDirectory_, "app.test-connection.dat"));
    }

    void TearDown() override {
        std::error_code cleanupError{};
        preferences_->Release();
        // This unique absolute directory was created by this fixture; no user database is targeted.
        std::filesystem::remove_all(settingsDirectory_, cleanupError);
    }
};

TEST_F(ClientUiSettingsTest, FirstLaunchHasNoStoredPosition) { EXPECT_FALSE(settings_.LoadControllerPosition()); }

TEST_F(ClientUiSettingsTest, PreferencesSurviveDatabaseCloseAndReopenWithoutChangingPosition) {
    ASSERT_TRUE(settings_.SaveControllerPosition({0.25F, 0.75F}));
    for (const auto preference : {ClientUiPreference::StatisticsVisible, ClientUiPreference::AudioEnabled, ClientUiPreference::English,
                                  ClientUiPreference::DarkTheme, ClientUiPreference::Fullscreen}) {
        EXPECT_FALSE(settings_.LoadPreference(preference));
        ASSERT_TRUE(settings_.SavePreference(preference, true));
    }
    preferences_->Release();
    ASSERT_TRUE(preferences_->Init(settingsDirectory_, "app.test-connection.dat"));
    for (const auto preference : {ClientUiPreference::StatisticsVisible, ClientUiPreference::AudioEnabled, ClientUiPreference::English,
                                  ClientUiPreference::DarkTheme, ClientUiPreference::Fullscreen}) {
        EXPECT_EQ(settings_.LoadPreference(preference), true);
        ASSERT_TRUE(settings_.SavePreference(preference, false));
    }
    preferences_->Release();
    ASSERT_TRUE(preferences_->Init(settingsDirectory_, "app.test-connection.dat"));
    EXPECT_EQ(settings_.LoadPreference(ClientUiPreference::StatisticsVisible), false);
    ASSERT_TRUE(settings_.LoadControllerPosition());
    EXPECT_FLOAT_EQ(settings_.LoadControllerPosition()->horizontalRatio, 0.25F);
}

TEST_F(ClientUiSettingsTest, InvalidBooleanUsesLaunchDefaultAndDoesNotBecomeFalse) {
    for (const std::string encoded : {"", "invalid", "2", "true", "-1"}) {
        ASSERT_TRUE(preferences_->Put("show_statistics", encoded));
        EXPECT_FALSE(settings_.LoadPreference(ClientUiPreference::StatisticsVisible));
    }
}

TEST_F(ClientUiSettingsTest, DisplayPreferencesSurviveReopenAndRejectInvalidValues) {
    ASSERT_TRUE(settings_.SaveFrameRate(90));
    ASSERT_TRUE(settings_.SaveResolution({1920, 1080}));
    EXPECT_FALSE(settings_.SaveFrameRate(0));
    EXPECT_FALSE(settings_.SaveFrameRate(47));
    EXPECT_FALSE(settings_.SaveResolution({0, 1080}));
    preferences_->Release();
    ASSERT_TRUE(preferences_->Init(settingsDirectory_, "app.test-connection.dat"));
    EXPECT_EQ(settings_.LoadFrameRate(), 90);
    const auto restored = settings_.LoadResolution();
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->width, 1920);
    EXPECT_EQ(restored->height, 1080);
    for (const std::string encoded : {"1920", "1920,", "1920,1080,1", "0,1080", "9000,1080"}) {
        ASSERT_TRUE(preferences_->Put("display_resolution", encoded));
        EXPECT_FALSE(settings_.LoadResolution());
    }
    for (const std::string encoded : {"", "60fps", "-1", "200", "47"}) {
        ASSERT_TRUE(preferences_->Put("display_frame_rate", encoded));
        EXPECT_FALSE(settings_.LoadFrameRate());
    }
}

TEST_F(ClientUiSettingsTest, LiveClientsShareTheDatabaseWithoutHoldingItsLevelDbLock) {
    preferences_->Release();
    const auto firstClient = ClientUiSettings::Open(settingsDirectory_, "app.test-connection.dat");
    const auto secondClient = ClientUiSettings::Open(settingsDirectory_, "app.test-connection.dat");
    ASSERT_TRUE(firstClient.SavePreference(ClientUiPreference::StatisticsVisible, true));
    EXPECT_EQ(secondClient.LoadPreference(ClientUiPreference::StatisticsVisible), true);
    ASSERT_TRUE(secondClient.SavePreference(ClientUiPreference::AudioEnabled, false));
    ASSERT_TRUE(secondClient.SaveControllerPosition({0.75F, 0.5F}));
    EXPECT_EQ(firstClient.LoadPreference(ClientUiPreference::AudioEnabled), false);
    EXPECT_EQ(firstClient.LoadPreference(ClientUiPreference::StatisticsVisible), true);
    ASSERT_TRUE(firstClient.LoadControllerPosition());
    EXPECT_FLOAT_EQ(firstClient.LoadControllerPosition()->horizontalRatio, 0.75F);
    EXPECT_EQ(ClientUiSettings::Open(settingsDirectory_, "app.test-connection.dat").LoadPreference(ClientUiPreference::StatisticsVisible), true);
}

TEST(ClientUiSettingsProcess, ChildWritesSharedTarget) {
    std::array<wchar_t, 32768> directoryBuffer{};
    const auto length =
        GetEnvironmentVariableW(L"PIXELS_TEST_UI_SETTINGS_DIRECTORY", directoryBuffer.data(), static_cast<DWORD>(directoryBuffer.size()));
    if (length == 0) GTEST_SKIP() << "Only executed by the interprocess settings fixture";
    ASSERT_LT(length, directoryBuffer.size());
    const auto settings = ClientUiSettings::Open(std::filesystem::path{std::wstring{directoryBuffer.data(), length}}, "app.shared.dat");
    for (int writeIndex{}; writeIndex < 20; ++writeIndex) {
        ASSERT_TRUE(settings.SavePreference(ClientUiPreference::AudioEnabled, false));
        EXPECT_EQ(settings.LoadPreference(ClientUiPreference::StatisticsVisible), true);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

TEST_F(ClientUiSettingsTest, SeparateProcessesWriteAndReopenTheSameTargetDatabase) {
    const auto parentSettings = ClientUiSettings::Open(settingsDirectory_, "app.shared.dat");
    ASSERT_TRUE(parentSettings.SavePreference(ClientUiPreference::StatisticsVisible, true));
    std::array<wchar_t, 32768> executableBuffer{};
    const auto length = GetModuleFileNameW(nullptr, executableBuffer.data(), static_cast<DWORD>(executableBuffer.size()));
    ASSERT_GT(length, 0U);
    ASSERT_LT(length, executableBuffer.size());
    std::wstring command =
        L"\"" + std::wstring{executableBuffer.data(), length} + L"\" --gtest_filter=ClientUiSettingsProcess.ChildWritesSharedTarget";
    ASSERT_TRUE(SetEnvironmentVariableW(L"PIXELS_TEST_UI_SETTINGS_DIRECTORY", settingsDirectory_.c_str()));
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION processInformation{};
    const bool created =
        CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &processInformation) != FALSE;
    px::UniqueWinHandle childProcess{processInformation.hProcess};
    px::UniqueWinHandle childThread{processInformation.hThread};
    processInformation = {};
    static_cast<void>(SetEnvironmentVariableW(L"PIXELS_TEST_UI_SETTINGS_DIRECTORY", nullptr));
    ASSERT_TRUE(created);
    for (int writeIndex{}; writeIndex < 20; ++writeIndex) {
        EXPECT_TRUE(parentSettings.SaveControllerPosition({0.25F, 0.75F}));
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const auto completion = WaitForSingleObject(childProcess.get(), 5000);
    if (completion != WAIT_OBJECT_0) {
        static_cast<void>(TerminateProcess(childProcess.get(), 1));
        static_cast<void>(WaitForSingleObject(childProcess.get(), 1000));
    }
    ASSERT_EQ(completion, WAIT_OBJECT_0);
    DWORD exitCode{};
    ASSERT_TRUE(GetExitCodeProcess(childProcess.get(), &exitCode));
    ASSERT_EQ(exitCode, 0U);
    const auto reopened = ClientUiSettings::Open(settingsDirectory_, "app.shared.dat");
    EXPECT_EQ(reopened.LoadPreference(ClientUiPreference::StatisticsVisible), true);
    EXPECT_EQ(reopened.LoadPreference(ClientUiPreference::AudioEnabled), false);
    ASSERT_TRUE(reopened.LoadControllerPosition());
    EXPECT_FLOAT_EQ(reopened.LoadControllerPosition()->horizontalRatio, 0.25F);
}

TEST_F(ClientUiSettingsTest, SavedPositionSurvivesNewSettingsInstanceAndOverwrite) {
    ASSERT_TRUE(settings_.SaveControllerPosition({0.25F, 0.75F}));
    preferences_->Release();
    ASSERT_TRUE(preferences_->Init(settingsDirectory_, "app.test-connection.dat"));
    const auto stored = ClientUiSettings{preferences_}.LoadControllerPosition();
    ASSERT_TRUE(stored);
    EXPECT_FLOAT_EQ(stored->horizontalRatio, 0.25F);
    EXPECT_FLOAT_EQ(stored->verticalRatio, 0.75F);
    ASSERT_TRUE(settings_.SaveControllerPosition({1.0F, 0.0F}));
    const auto updated = ClientUiSettings{preferences_}.LoadControllerPosition();
    ASSERT_TRUE(updated);
    EXPECT_FLOAT_EQ(updated->horizontalRatio, 1.0F);
    EXPECT_FLOAT_EQ(updated->verticalRatio, 0.0F);
}

TEST_F(ClientUiSettingsTest, InvalidSaveDoesNotReplaceGoodPosition) {
    ASSERT_TRUE(settings_.SaveControllerPosition({0.5F, 0.5F}));
    EXPECT_FALSE(settings_.SaveControllerPosition({-1.0F, 0.5F}));
    const auto stored = settings_.LoadControllerPosition();
    ASSERT_TRUE(stored);
    EXPECT_FLOAT_EQ(stored->horizontalRatio, 0.5F);
}

TEST_F(ClientUiSettingsTest, CorruptMissingAndOutOfRangeValuesAreIgnored) {
    for (const std::string invalidValue : {"invalid", "", "0.5", "0.5,", "0.5,2", "-1,0.5", "0.5,nan", "0.5,0.2,trailing"}) {
        ASSERT_TRUE(preferences_->Put("float_button_position_ratio", invalidValue));
        EXPECT_FALSE(settings_.LoadControllerPosition());
    }
}

TEST_F(ClientUiSettingsTest, IndependentConnectionsDoNotSharePositions) {
    ASSERT_TRUE(settings_.SaveControllerPosition({0.25F, 0.75F}));
    {
        const auto otherSettings = ClientUiSettings::Open(settingsDirectory_, "app.other-connection.dat");
        EXPECT_FALSE(otherSettings.LoadControllerPosition());
        ASSERT_TRUE(otherSettings.SaveControllerPosition({0.9F, 0.1F}));
        const auto original = settings_.LoadControllerPosition();
        ASSERT_TRUE(original);
        EXPECT_FLOAT_EQ(original->horizontalRatio, 0.25F);
        EXPECT_FLOAT_EQ(original->verticalRatio, 0.75F);
    }
    const auto restoredOther = ClientUiSettings::Open(settingsDirectory_, "app.other-connection.dat").LoadControllerPosition();
    ASSERT_TRUE(restoredOther);
    EXPECT_FLOAT_EQ(restoredOther->horizontalRatio, 0.9F);
    EXPECT_FLOAT_EQ(restoredOther->verticalRatio, 0.1F);
}

TEST(ClientUiSettings, DatabaseNamesIsolateTargetConsoleProductAndWorkspace) {
    const auto original = ClientUiSettings::DatabaseName("client", "https://console.example:4600", "target-device", "");
    EXPECT_EQ(original, ClientUiSettings::DatabaseName("client", "https://console.example:4600", "target-device", ""));
    EXPECT_NE(original, ClientUiSettings::DatabaseName("client", "https://console.example:4600", "other-device", ""));
    EXPECT_NE(original, ClientUiSettings::DatabaseName("client", "https://other-console.example:4600", "target-device", ""));
    EXPECT_NE(original, ClientUiSettings::DatabaseName("remote", "https://console.example:4600", "target-device", ""));
    EXPECT_NE(original, ClientUiSettings::DatabaseName("client", "https://console.example:4600", "target-device", "workspace-user"));
    EXPECT_TRUE(original.starts_with("app."));
    EXPECT_TRUE(original.ends_with(".dat"));
}

TEST(ClientUiSettings, UnavailablePathIsNonFatal) {
    const ClientUiSettings settings{std::shared_ptr<px::SharedPreference>{}};
    EXPECT_FALSE(settings.LoadControllerPosition());
    EXPECT_FALSE(settings.SaveControllerPosition({0.5F, 0.5F}));
    EXPECT_FALSE(settings.LoadPreference(ClientUiPreference::StatisticsVisible));
    EXPECT_FALSE(settings.SavePreference(ClientUiPreference::StatisticsVisible, true));
}

}  // namespace
}  // namespace px::client::imgui
