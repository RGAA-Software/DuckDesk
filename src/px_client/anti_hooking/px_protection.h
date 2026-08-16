#pragma once

// Anti-hooking protection (compiled statically into px_client.exe).
// Hooks LoadLibrary{A,W,ExA,ExW} via Detours and blocks a blacklist of
// known-bad third-party overlay/hook DLLs (Nahimic audio OSD, RTSS, Optimus
// init hooks, ...) that otherwise crash or deadlock D3D9/DXVA2/Vulkan
// rendering in the client.

// Install the hooks. Call once, early in main().
void PxEnableAntiHookingProtection();

// Remove the hooks (optional; process teardown removes them anyway).
void PxDisableAntiHookingProtection();
