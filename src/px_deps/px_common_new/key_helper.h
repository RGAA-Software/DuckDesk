//
// Created by RGAA on 11/08/2024.
//

#ifndef PX_KEY_HELPER_H
#define PX_KEY_HELPER_H

namespace px
{

    class KeyHelper {
    public:
        static bool IsKeyPressed(int vk);
        static bool IsShiftPressed();
        static bool IsControlPressed();
        static bool IsAltPressed();
        static bool IsWinPressed();
        static bool IsCapsLockPressed();
        static bool IsNumLockPressed();
        static int GetKeyStateInner(int vk);
        static int GetCapsLockState();
        static int GetNumLockState();
    };

}

#endif //PX_KEY_HELPER_H
