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
!define PRODUCT_NAME "GoDesk"
!define APPNAME "px_panel"
!define COMPANY "GoDesk"
!define INSTALL_DIR "C:\Program Files\GoDesk\App"

!ifndef OUTPUT_DIR
    !define OUTPUT_DIR "."
!endif

OutFile "${OUTPUT_DIR}\${PRODUCT_NAME}_${PRODUCT_VERSION}_${TARGET_TYPE}_Setup.exe"

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

    ; 2. Install ViGEm joystick driver silently
    ExecWait '"$INSTDIR\px_joystick.exe" /S'

    ; 3. Create shortcuts
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    ; 4. Write uninstall registry info
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
    ; Delete files
    RMDir /r "$INSTDIR"

    ; Delete the auto-start panel scheduled task (default behavior)
    nsExec::ExecToLog 'schtasks /Delete /TN px_panel_start /F'

    ; Delete shortcuts
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"

    ; Delete registry entries
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANY} ${APPNAME}"
    DeleteRegKey HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers\$INSTDIR\${APPNAME}.exe"

SectionEnd

;--------------------------------
Function .onInit
    ; Check whether the app is already running
    ${nsProcess::FindProcess} "${APPNAME}.exe" $R0
    ${If} $R0 == 0
        MessageBox MB_OK|MB_TOPMOST "Please close the running program and reinstall"
        Call StopAndDeleteService
        Call KillProcesses

        Call StopAndDeleteService
        Call KillProcesses
    ${EndIf}
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
