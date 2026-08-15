//
// Created by RGAA on 11/08/2024.
//

#ifndef GAMMARAY_KEY_HELPER_H
#define GAMMARAY_KEY_HELPER_H

namespace tc
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

#endif //GAMMARAY_KEY_HELPER_H
