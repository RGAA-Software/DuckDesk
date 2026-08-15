#pragma once

namespace tc
{

    /* Contains the necessary Windows SDK header files */
    #include "windows.h"
    #include "winternl.h"
    #include "stdlib.h"

    /* Defining the request result status code in NTSTATUS */
    #define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

    /* Automatically selects the corresponding API based on the character set type of the current project */
    #ifdef _UNICODE
    typedef WCHAR* PCMDBUFFER_T;
    #define GetProcessCommandLine GetProcessCommandLineW
    #else
    typedef CHAR* PCMDBUFFER_T;
    #define GetProcessCommandLine GetProcessCommandLineA
    #endif

    /* Multiple version declarations for GetProcessCommandLine */
    BOOL
    WINAPI
    GetProcessCommandLineW(
        _In_        HANDLE        hProcess,
        _In_opt_    LPCWSTR        lpcBuffer,
        _In_opt_    SIZE_T        nSize,
        _In_opt_    SIZE_T* lpNumberOfBytesCopied
    );

    BOOL
    WINAPI
    GetProcessCommandLineA(
        _In_        HANDLE        hProcess,
        _In_opt_    LPCSTR        lpcBuffer,
        _In_opt_    SIZE_T        nSize,
        _In_opt_    SIZE_T* lpNumberOfBytesCopied
    );

}