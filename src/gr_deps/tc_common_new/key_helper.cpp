//
// Created by RGAA on 11/08/2024.
//

#include "key_helper.h"
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace tc
{

    bool KeyHelper::IsKeyPressed(int vk) {
#ifdef WIN32
        return GetKeyState(vk) < 0;
#else
        return false;
#endif
    }

    bool KeyHelper::IsShiftPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_LSHIFT) || IsKeyPressed(VK_RSHIFT);
#else
        return false;
#endif
    }

    bool KeyHelper::IsControlPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_LCONTROL) || IsKeyPressed(VK_RCONTROL);
#else
        return false;
#endif
    }

    bool KeyHelper::IsAltPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_LMENU) || IsKeyPressed(VK_RMENU);
#else
        return false;
#endif
    }

    bool KeyHelper::IsWinPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_LWIN) || IsKeyPressed(VK_RWIN);
#else
        return false;
#endif
    }

    bool KeyHelper::IsCapsLockPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_CAPITAL);
#else
        return false;
#endif
    }

    bool KeyHelper::IsNumLockPressed() {
#ifdef WIN32
        return IsKeyPressed(VK_NUMLOCK);
#else
        return false;
#endif
    }

    int KeyHelper::GetKeyStateInner(int vk) {
#ifdef WIN32
        return GetKeyState(vk);
#else
        return -1;
#endif
    }

    int KeyHelper::GetCapsLockState() {
#ifdef WIN32
        return KeyHelper::GetKeyStateInner(VK_CAPITAL);
#else
        return -22;
#endif
    }

    int KeyHelper::GetNumLockState() {
#ifdef WIN32
        return GetKeyState(VK_NUMLOCK);
#else
        return -22;
#endif
    }

}