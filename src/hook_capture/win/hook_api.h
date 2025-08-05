//
// Created by RGAA on 2024/3/1.
//

#ifndef TC_APPLICATION_HOOK_API_H
#define TC_APPLICATION_HOOK_API_H

#include <windows.h>
#include <unknwnbase.h>

// hook api

typedef UINT (WINAPI *GetRawInputData_t)(
        HRAWINPUT hRawInput,
        UINT uiCommand,
        LPVOID pData,
        PUINT pcbSize,
        UINT cbSizeHeader);

typedef UINT (WINAPI *GetRawInputBuffer_t)(
        PRAWINPUT pData,
        PUINT pcbSize,
        UINT cbSizeHeader);

typedef BOOL(WINAPI *PostMessageA_t)(
        HWND hWnd,
        UINT Msg,
        WPARAM wParam,
        LPARAM lParam);

typedef	BOOL(WINAPI *PostMessageW_t)(
        HWND hWnd,
        UINT Msg,
        WPARAM wParam,
        LPARAM lParam);

typedef LRESULT(WINAPI *SendMessageA_t)(
        HWND hWnd,
        UINT Msg,
        WPARAM wParam,
        LPARAM lParam);

typedef LRESULT(WINAPI* SendMessageW_t)(
        HWND hWnd,
        UINT Msg,
        WPARAM wParam,
        LPARAM lParam);

typedef BOOL (WINAPI* GetCursorPos_t)(LPPOINT lpPoint);
typedef BOOL (WINAPI* SetCursorPos_t)(int X, int Y);

typedef SHORT (*GetAsyncKeyState_t)(int vKey);
typedef SHORT (*GetKeyState_t)(int nVirtKey);
typedef BOOL (WINAPI *IsWindowVisibleHooked_t)(_In_ HWND hWnd);
typedef HWND (WINAPI *GetForegroundWindowHooked_t)(VOID);

typedef HRESULT (*DirectInput8Create_t)(
        HINSTANCE hinst,
        DWORD dwVersion,
        REFIID riidltf,
        LPVOID * ppvOut,
        LPUNKNOWN punkOuter
);

typedef HWND (WINAPI* WindowFromPoint_t)(_In_ POINT Point);
typedef BOOL (WINAPI* ClipCursor_t)(_In_opt_ CONST RECT *lpRect);

// hook api

#endif //TC_APPLICATION_HOOK_API_H
