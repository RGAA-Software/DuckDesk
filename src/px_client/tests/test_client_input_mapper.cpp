#include "client_input_mapper.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <Windows.h>
#include <gtest/gtest.h>

namespace px::client::imgui {

TEST(ClientInputMapperTest, MapsNavigationAndFunctionKeys) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_A), 'A');
    EXPECT_EQ(WindowsVirtualKey(SDLK_7), '7');
    EXPECT_EQ(WindowsVirtualKey(SDLK_LEFT), VK_LEFT);
    EXPECT_EQ(WindowsVirtualKey(SDLK_F12), VK_F12);
}

TEST(ClientInputMapperTest, PreservesModifierSidesAndOemKeys) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_LCTRL), VK_LCONTROL);
    EXPECT_EQ(WindowsVirtualKey(SDLK_RCTRL), VK_RCONTROL);
    EXPECT_EQ(WindowsVirtualKey(SDLK_LSHIFT), VK_LSHIFT);
    EXPECT_EQ(WindowsVirtualKey(SDLK_RSHIFT), VK_RSHIFT);
    EXPECT_EQ(WindowsVirtualKey(SDLK_LALT), VK_LMENU);
    EXPECT_EQ(WindowsVirtualKey(SDLK_RALT), VK_RMENU);
    EXPECT_EQ(WindowsVirtualKey(SDLK_LGUI), VK_LWIN);
    EXPECT_EQ(WindowsVirtualKey(SDLK_RGUI), VK_RWIN);
    EXPECT_EQ(WindowsVirtualKey(SDLK_SEMICOLON), VK_OEM_1);
    EXPECT_EQ(WindowsVirtualKey(SDLK_APOSTROPHE), VK_OEM_7);
}

TEST(ClientInputMapperTest, PreservesWindowsScanCodesAndExtendedPrefixes) {
    const auto leftShift = WindowsKeyFromSdl(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, 0x2A);
    const auto rightShift = WindowsKeyFromSdl(SDLK_RSHIFT, SDL_SCANCODE_RSHIFT, 0x36);
    const auto leftControl = WindowsKeyFromSdl(SDLK_LCTRL, SDL_SCANCODE_LCTRL, 0x1D);
    const auto rightControl = WindowsKeyFromSdl(SDLK_RCTRL, SDL_SCANCODE_RCTRL, 0xE01D);
    const auto rightAlt = WindowsKeyFromSdl(SDLK_RALT, SDL_SCANCODE_RALT, 0xE038);

    EXPECT_EQ(leftShift.virtualKey, VK_LSHIFT);
    EXPECT_EQ(leftShift.scanCode, 0x2AU);
    EXPECT_EQ(rightShift.virtualKey, VK_RSHIFT);
    EXPECT_EQ(rightShift.scanCode, 0x36U);
    EXPECT_EQ(leftControl.virtualKey, VK_LCONTROL);
    EXPECT_EQ(leftControl.scanCode, 0x1DU);
    EXPECT_EQ(rightControl.virtualKey, VK_RCONTROL);
    EXPECT_EQ(rightControl.scanCode, 0xE01DU);
    EXPECT_EQ(rightAlt.virtualKey, VK_RMENU);
    EXPECT_EQ(rightAlt.scanCode, 0xE038U);
}

TEST(ClientInputMapperTest, UsesPhysicalScanCodeForShiftedSymbols) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_AT, SDL_SCANCODE_2), '2');
    EXPECT_EQ(WindowsVirtualKey(SDLK_EXCLAIM, SDL_SCANCODE_1), '1');
    EXPECT_EQ(WindowsVirtualKey(SDLK_COLON, SDL_SCANCODE_SEMICOLON), VK_OEM_1);
}

TEST(ClientInputMapperTest, MapsNumericKeypad) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_0), VK_NUMPAD0);
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_5), VK_NUMPAD5);
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_DIVIDE), VK_DIVIDE);
}

TEST(ClientInputMapperTest, DistinguishesMainAndKeypadEnter) {
    const auto mainEnter = WindowsKeyFromSdl(SDLK_RETURN, SDL_SCANCODE_RETURN, 0x1C);
    const auto keypadEnter = WindowsKeyFromSdl(SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER, 0xE01C);

    EXPECT_EQ(mainEnter.virtualKey, VK_RETURN);
    EXPECT_EQ(mainEnter.scanCode, 0x1CU);
    EXPECT_EQ(keypadEnter.virtualKey, VK_RETURN);
    EXPECT_EQ(keypadEnter.scanCode, 0xE01CU);
}

TEST(ClientInputMapperTest, MapsExtendedFunctionNavigationAndMediaKeys) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_F24, SDL_SCANCODE_F24), VK_F24);
    EXPECT_EQ(WindowsVirtualKey(SDLK_PRINTSCREEN, SDL_SCANCODE_PRINTSCREEN), VK_SNAPSHOT);
    EXPECT_EQ(WindowsVirtualKey(SDLK_APPLICATION, SDL_SCANCODE_APPLICATION), VK_APPS);
    EXPECT_EQ(WindowsVirtualKey(0, SDL_SCANCODE_NONUSBACKSLASH), VK_OEM_102);
    EXPECT_EQ(WindowsVirtualKey(SDLK_VOLUMEUP, SDL_SCANCODE_VOLUMEUP), VK_VOLUME_UP);
    EXPECT_EQ(WindowsVirtualKey(SDLK_MEDIA_NEXT_TRACK, SDL_SCANCODE_MEDIA_NEXT_TRACK), VK_MEDIA_NEXT_TRACK);
    EXPECT_EQ(WindowsVirtualKey(SDLK_AC_BACK, SDL_SCANCODE_AC_BACK), VK_BROWSER_BACK);
}

TEST(ClientInputMapperTest, DetectsCommittedUnicodeText) {
    EXPECT_FALSE(ContainsNonAscii("Pixels 123"));
    EXPECT_TRUE(ContainsNonAscii("中文输入"));
}

} // namespace px::client::imgui
