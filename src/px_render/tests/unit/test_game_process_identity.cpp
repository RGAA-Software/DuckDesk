#include <gtest/gtest.h>
#include "../../app/win/game_process_identity.h"
#include "../../app/game_frame_identity.h"
#include <array>

TEST(GameProcessIdentity, RejectsSameBasenameInAnotherDirectoryOrDrive) {
    EXPECT_FALSE(px::SameGameExecutable(L"G:/app/2dadventure/2dAdventure.exe", L"D:/software/2dadventure/2dAdventure.exe"));
    EXPECT_FALSE(px::SameGameExecutable(L"G:/app/2dAdventure.exe", L"G:/other/2dAdventure.exe"));
    EXPECT_FALSE(px::SameGameExecutable(L"G:/app/2dAdventure.exe", L"G:/app/another.exe"));
}

TEST(GameProcessIdentity, NormalizesSeparatorsDotSegmentsAndWindowsCase) {
    EXPECT_TRUE(px::SameGameExecutable(L"G:/app/2dadventure/2dAdventure.exe", L"g:\\APP\\2dadventure\\2DADVENTURE.EXE"));
    EXPECT_TRUE(px::SameGameExecutable(L"G:/app/./game.exe", L"G:/app/temp/../game.exe"));
    EXPECT_TRUE(px::SameGameExecutable(L"C:/\u6d4b\u8bd5/\u00c4/game.exe", L"c:/\u6d4b\u8bd5/\u00e4/GAME.EXE"));
    EXPECT_TRUE(px::SameGameExecutable(L"//server/share/game.exe", L"\\\\SERVER\\SHARE\\GAME.EXE"));
}

TEST(GameProcessIdentity, RejectsUnavailableAndRelativePaths) {
    EXPECT_FALSE(px::SameGameExecutable({}, L"G:/app/game.exe"));
    EXPECT_FALSE(px::SameGameExecutable(L"G:/app/game.exe", {}));
    EXPECT_FALSE(px::SameGameExecutable(L"game.exe", L"game.exe"));
    EXPECT_FALSE(px::SameGameExecutable(L"G:app/game.exe", L"G:/app/game.exe"));
}

TEST(GameProcessIdentity, PreservesSpacesAndAcceptsBalancedQuotesAndExtendedPrefixes) {
    EXPECT_TRUE(px::SameGameExecutable(LR"("C:/Program Files/My Game/game.exe")", LR"(c:\Program Files\My Game\GAME.EXE)"));
    EXPECT_TRUE(px::SameGameExecutable(LR"(\\?\C:\Program Files\My Game\game.exe)", L"C:/Program Files/My Game/game.exe"));
    EXPECT_TRUE(px::SameGameExecutable(LR"(\\?\unc\Server\My Share\game.exe)", L"//server/My Share/GAME.EXE"));
    EXPECT_FALSE(px::SameGameExecutable(L"C:/My Game/game.exe", L"C:/MyGame/game.exe"));
    EXPECT_FALSE(px::NormalizeGameExecutable(L"\"C:/My Game/game.exe"));
    EXPECT_FALSE(px::NormalizeGameExecutable(L"C:/My Game/game.exe\""));
    EXPECT_FALSE(px::NormalizeGameExecutable(LR"(\\.\PhysicalDrive0)"));
    EXPECT_FALSE(px::NormalizeGameExecutable(L"steam://run/123"));
    EXPECT_FALSE(px::NormalizeGameExecutable(std::wstring(L"C:/game.exe\0other.exe", 21)));
}

TEST(GameProcessIdentity, KeepsWindowsArgumentTailIntact) {
    const std::wstring arguments{LR"(--name "My Game" --dir "C:\save data\\" --empty "" --literal "a\"b")"};
    const auto command = px::GameCommandLine(LR"("C:/Program Files/My Game/game.exe")", arguments);
    ASSERT_TRUE(command);
    EXPECT_EQ(*command, LR"("C:\Program Files\My Game\game.exe" )" + arguments);
    EXPECT_EQ(px::GameCommandLine(L"C:/My Game/game.exe", L""), LR"("C:\My Game\game.exe")");
    EXPECT_FALSE(px::GameCommandLine(L"C:/game.exe", std::wstring(L"one\0two", 7)));
}

TEST(GameFrameIdentity, MissingIpcIdentityIsStableAcrossFramesAndReconnect) {
    std::array<char, 64> name{};
    ASSERT_TRUE(px::EnsureGameFrameIdentity(name));
    EXPECT_EQ(std::string(name.data()), "game-hook");
    const auto original = name;
    ASSERT_TRUE(px::EnsureGameFrameIdentity(name));
    EXPECT_EQ(name, original);
    std::array<char, 64> reconnect{};
    ASSERT_TRUE(px::EnsureGameFrameIdentity(reconnect));
    EXPECT_EQ(name, reconnect);
}

TEST(GameFrameIdentity, PreservesExplicitIdentityAndRejectsInvalidBuffer) {
    std::array<char, 16> name{'g', 'a', 'm', 'e', '-', '2'};
    const auto original = name;
    ASSERT_TRUE(px::EnsureGameFrameIdentity(name));
    EXPECT_EQ(name, original);
    std::array<char, 2> too_small{};
    EXPECT_FALSE(px::EnsureGameFrameIdentity(too_small));
    std::array<char, 2> unterminated{'a', 'b'};
    EXPECT_FALSE(px::EnsureGameFrameIdentity(unterminated));
    EXPECT_FALSE(px::EnsureGameFrameIdentity({}));
}
