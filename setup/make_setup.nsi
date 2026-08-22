;--------------------------------
; Modern UI
!include "MUI2.nsh"
!include "x64.nsh"
!include "nsProcess.nsh"
!include "proj_version.nsh"

Unicode true
RequestExecutionLevel admin

;--------------------------------
; App Info
!define PRODUCT_NAME "Pixels"
!define APPNAME "px_panel"
!define COMPANY "Pixels"
!define INSTALL_DIR "C:\Program Files\PixelsRender"

!ifndef OUTPUT_DIR
    !define OUTPUT_DIR "."
!endif

OutFile "${OUTPUT_DIR}\${PRODUCT_NAME}_${PRODUCT_VERSION}_Setup.exe"

InstallDir "${INSTALL_DIR}"

Name "${PRODUCT_NAME}"

;--------------------------------
!define MUI_ICON "image\logo.ico"
!define MUI_UNICON "image\uninstall.ico"

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "image\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "image\welcome.bmp"

!define MUI_ABORTWARNING

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Sections
Section "Install required files" SecMain

    SetOutPath "$INSTDIR"

    ; Clean stale plugins/skins from previous installs.
    ; Old DLLs no longer shipped (e.g. net_udp.dll) are ABI-incompatible
    ; and crash the render process when the plugin loader scans this directory.
    RMDir /r "$INSTDIR\px_plugins"
    RMDir /r "$INSTDIR\px_plugins_client"
    RMDir /r "$INSTDIR\px_skins"
    RMDir /r "$INSTDIR\px_client"
    RMDir /r "$INSTDIR\deps"

    ; 1. Extract app.7z
    File "${OUTPUT_DIR}\app\app.7z"
    Nsis7z::ExtractWithCallback "$INSTDIR\app.7z" $R9
    Delete "$INSTDIR\app.7z"

    ; 2. Install the Amyuni USBMMIDD virtual display driver. Keep the working
    ; directory on the driver payload, matching RustDesk's invocation.
    Call InstallUsbMmIddDriver
    Pop $R0
    StrCmp $R0 "0" usbmmidd_install_ok
        IfSilent +2
            MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "Failed to install the USBMMIDD virtual display driver. Setup cannot continue."
        SetErrorLevel 1603
        Abort "USBMMIDD virtual display driver installation failed"
usbmmidd_install_ok:

    ; 3. Install ViGEm joystick driver silently
    ExecWait '"$INSTDIR\px_joystick.exe" /S'

    ; 4. Create shortcuts
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    ; 5. Write uninstall registry info
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}" "Publisher" "${COMPANY}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}" "DisplayVersion" "${PRODUCT_VERSION}"

    ; Set the app to run as administrator
    WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\${APPNAME}.exe" "RUNASADMIN"

    ; Write the uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    Call LaunchLink
SectionEnd

;--------------------------------
; Uninstaller
Section "Uninstall"
    ; The USBMMIDD removal tool must still be present, so remove the driver
    ; before deleting the installation directory.
    Call un.UninstallUsbMmIddDriver
    Pop $R0
    StrCmp $R0 "0" usbmmidd_uninstall_ok
        IfSilent +2
            MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "Failed to remove the USBMMIDD virtual display driver. Uninstall cannot continue."
        SetErrorLevel 1603
        Abort "USBMMIDD virtual display driver removal failed"
usbmmidd_uninstall_ok:

    ; Delete files
    ; The driver function used $INSTDIR as its working directory. Move away
    ; first so Windows can remove the now-empty installation root as well.
    SetOutPath "$TEMP"
    RMDir /r "$INSTDIR"

    ; Delete the auto-start panel scheduled task (default behavior)
    nsExec::ExecToLog 'schtasks /Delete /TN px_panel_start /F'

    ; Delete shortcuts
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; Delete registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}"
    DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\${APPNAME}.exe"

SectionEnd

;--------------------------------
Function .onInit
    ; Check whether the app is already running
    ${nsProcess::FindProcess} "${APPNAME}.exe" $R0
    ${If} $R0 == 0
        IfSilent +2
            MessageBox MB_OK|MB_TOPMOST "Please close the running program and reinstall"
    ${EndIf}

    ; A background service or render process may still be active even when the
    ; panel is not. Always stop the complete product before replacing DLLs.
    Call StopAndDeleteService
    Call KillProcesses

    Call StopAndDeleteService
    Call KillProcesses
FunctionEnd

Function un.onInit
    Call un.StopAndDeleteService
    Call un.KillProcesses

    Call un.StopAndDeleteService
    Call un.KillProcesses
FunctionEnd

Function LaunchLink
    ExecShell "" "$INSTDIR\${APPNAME}.exe"
FunctionEnd

; Return "0" on success and "1" on failure. deviceinstaller64 starts the
; Plug-and-Play work asynchronously, so its process exit code alone is not a
; reliable result. Poll PnP until a healthy display device appears.
Function InstallUsbMmIddDriver
    IfFileExists "$INSTDIR\usbmmidd_v2\deviceinstaller64.exe" +2 0
        Goto usbmmidd_install_failed
    IfFileExists "$INSTDIR\usbmmidd_v2\usbmmIdd.inf" +2 0
        Goto usbmmidd_install_failed

    ; Match RustDesk's idempotent behavior: do not create another ROOT\DISPLAY
    ; device when USBMMIDD is already installed during an upgrade/repair.
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'USB Mobile Monitor Virtual Display' -and $$_.Status -eq 'OK' }); if ($$device.Count -gt 0) { exit 0 }; exit 1"`
    Pop $R3
    Pop $R4
    StrCmp $R3 "0" usbmmidd_install_already_present

    DetailPrint "Installing USBMMIDD virtual display driver..."
    SetOutPath "$INSTDIR\usbmmidd_v2"
    nsExec::ExecToStack '"$INSTDIR\usbmmidd_v2\deviceinstaller64.exe" install usbmmidd.inf usbmmidd'
    Pop $R0
    Pop $R1
    DetailPrint "USBMMIDD installer exit code: $R0"
    DetailPrint "$R1"

    StrCpy $R2 0
usbmmidd_install_verify:
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'USB Mobile Monitor Virtual Display' -and $$_.Status -eq 'OK' }); if ($$device.Count -gt 0) { exit 0 }; exit 1"`
    Pop $R3
    Pop $R4
    StrCmp $R3 "0" usbmmidd_install_verified
    IntOp $R2 $R2 + 1
    StrCmp $R2 "60" usbmmidd_install_failed
    Sleep 500
    Goto usbmmidd_install_verify

usbmmidd_install_verified:
    DetailPrint "USBMMIDD virtual display driver is installed."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

usbmmidd_install_already_present:
    DetailPrint "USBMMIDD virtual display driver is already installed; skipping device creation."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

usbmmidd_install_failed:
    DetailPrint "USBMMIDD virtual display driver was not detected after installation."
    SetOutPath "$INSTDIR"
    Push "1"
FunctionEnd

; Remove all USBMMIDD monitors and the root display device before deleting the
; bundled deviceinstaller64.exe. This is the vendor/RustDesk removal path.
Function un.UninstallUsbMmIddDriver
    IfFileExists "$INSTDIR\usbmmidd_v2\deviceinstaller64.exe" +3 0
        DetailPrint "USBMMIDD removal tool is absent; verifying that the driver is absent."
        Goto usbmmidd_uninstall_verify

    DetailPrint "Removing USBMMIDD virtual display driver..."
    SetOutPath "$INSTDIR\usbmmidd_v2"
    nsExec::ExecToLog '"$INSTDIR\usbmmidd_v2\deviceinstaller64.exe" stop usbmmidd'
    nsExec::ExecToStack '"$INSTDIR\usbmmidd_v2\deviceinstaller64.exe" remove usbmmidd'
    Pop $R0
    Pop $R1
    DetailPrint "USBMMIDD remover exit code: $R0"
    DetailPrint "$R1"

usbmmidd_uninstall_verify:
    StrCpy $R2 0
usbmmidd_uninstall_verify_loop:
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'USB Mobile Monitor Virtual Display' }); if ($$device.Count -eq 0) { exit 0 }; exit 1"`
    Pop $R3
    Pop $R4
    StrCmp $R3 "0" usbmmidd_uninstall_verified
    IntOp $R2 $R2 + 1
    StrCmp $R2 "60" usbmmidd_uninstall_failed
    Sleep 500
    Goto usbmmidd_uninstall_verify_loop

usbmmidd_uninstall_verified:
    DetailPrint "USBMMIDD device node is removed; deleting its staged driver package..."
    ; deviceinstaller64 follows the RustDesk/vendor device-removal path, but
    ; Windows normally leaves the signed INF staged in Driver Store. The
    ; product owns this package, so remove only the exact Amyuni usbmmIdd.inf
    ; package as part of a full product uninstall.
    SetOutPath "$PLUGINSDIR"
    File /oname=remove_usbmmidd_driver_store.ps1 "remove_usbmmidd_driver_store.ps1"
    ${DisableX64FSRedirection}
    nsExec::ExecToStack '"$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\remove_usbmmidd_driver_store.ps1"'
    ${EnableX64FSRedirection}
    Pop $R3
    Pop $R4
    DetailPrint "$R4"
    StrCmp $R3 "0" usbmmidd_driver_store_removed
    Goto usbmmidd_uninstall_failed

usbmmidd_driver_store_removed:
    DetailPrint "USBMMIDD virtual display driver and Driver Store package are removed."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

usbmmidd_uninstall_failed:
    DetailPrint "USBMMIDD virtual display driver is still present."
    SetOutPath "$INSTDIR"
    Push "1"
FunctionEnd

Function StopAndDeleteService
    ; net stop synchronization: first ensure the service is stopped and removed,
    ; cutting off the restart source
    ; (it restarts render/UserProxy every 3s; UserProxy restarts panel/SysInfo every 5s)
    nsExec::ExecToLog 'net stop "px_service"'
    nsExec::ExecToLog 'sc delete "px_service"'
FunctionEnd

Function un.StopAndDeleteService
    nsExec::ExecToLog 'net stop "px_service"'
    nsExec::ExecToLog 'sc delete "px_service"'
FunctionEnd


Function KillProcesses
    ; Order: kill the guardian (UserProxy) first, then the guard processes;
    ; SysInfo needs only one kill
    nsExec::ExecToLog 'taskkill /F /T /IM px_function.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_client.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_render.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_panel.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_osinfo.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service_manager.exe'
FunctionEnd

Function un.KillProcesses
    nsExec::ExecToLog 'taskkill /F /T /IM px_function.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_client.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_render.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_panel.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_osinfo.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service_manager.exe'
FunctionEnd
