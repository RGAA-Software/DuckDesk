#include "client_input_mapper.h"

#include <SDL3/SDL_keycode.h>
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
    EXPECT_EQ(WindowsVirtualKey(SDLK_LALT), VK_LMENU);
    EXPECT_EQ(WindowsVirtualKey(SDLK_RALT), VK_RMENU);
    EXPECT_EQ(WindowsVirtualKey(SDLK_SEMICOLON), VK_OEM_1);
    EXPECT_EQ(WindowsVirtualKey(SDLK_APOSTROPHE), VK_OEM_7);
}

TEST(ClientInputMapperTest, MapsNumericKeypad) {
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_0), VK_NUMPAD0);
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_5), VK_NUMPAD5);
    EXPECT_EQ(WindowsVirtualKey(SDLK_KP_DIVIDE), VK_DIVIDE);
}

TEST(ClientInputMapperTest, DetectsCommittedUnicodeText) {
    EXPECT_FALSE(ContainsNonAscii("Pixels 123"));
    EXPECT_TRUE(ContainsNonAscii("中文输入"));
}

} // namespace px::client::imgui
