Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef PAYLOAD_DIR
    !error "PAYLOAD_DIR is required"
!endif
!ifndef SUITE_VERSION
    !error "SUITE_VERSION is required"
!endif
!ifndef MANIFEST_SHA256
    !error "MANIFEST_SHA256 is required"
!endif
!ifndef OUTPUT_FILE
    !error "OUTPUT_FILE is required"
!endif

Name "Pixels Server ${SUITE_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\Pixels\Server"
InstallDirRegKey HKLM "Software\Pixels\SingleServer" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

Var ConfigRoot
Var DataRoot
Var OpenConsolePage
Var InstallOutput
Var RecoveryArgument

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString RequiresX64 ${LANG_ENGLISH} "Pixels Server requires Windows x86_64."
LangString RequiresX64 ${LANG_SIMPCHINESE} "Pixels Server 需要 Windows x86_64。"
LangString InstallFailed ${LANG_ENGLISH} "Pixels Server installation failed (exit $0). Existing configuration and data were retained."
LangString InstallFailed ${LANG_SIMPCHINESE} "Pixels Server 安装失败（退出码 $0），原有配置和数据已保留。"
LangString UninstallFailed ${LANG_ENGLISH} "Pixels Server uninstall failed (exit $0). Configuration and data were retained."
LangString UninstallFailed ${LANG_SIMPCHINESE} "Pixels Server 卸载失败（退出码 $0），配置和数据已保留。"

Function .onInit
    SetRegView 64
    SetShellVarContext all
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "$(RequiresX64)"
        Abort
    ${EndIf}
    StrCpy $ConfigRoot "$APPDATA\Pixels\Server\config"
    StrCpy $DataRoot "$APPDATA\Pixels\Server\data"
    ReadRegStr $0 HKLM "Software\Pixels\SingleServer" "ConfigRoot"
    StrCmp $0 "" +2
        StrCpy $ConfigRoot $0
    ReadRegStr $0 HKLM "Software\Pixels\SingleServer" "DataRoot"
    StrCmp $0 "" +2
        StrCpy $DataRoot $0
    ReadRegStr $0 HKLM "Software\Pixels\SingleServer" "InstallLocation"
    StrCmp $0 "" installation_location_ready
        StrCpy $INSTDIR $0
    installation_location_ready:
    StrCpy $RecoveryArgument ""
    ${GetParameters} $1
    ClearErrors
    ${GetOptions} $1 "/RECOVEROWNER" $2
    IfErrors owner_recovery_ready
        StrCpy $RecoveryArgument "-RecoverDatabaseOwner"
    owner_recovery_ready:
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Function .onGUIEnd
    StrCmp $OpenConsolePage "1" 0 console_page_done
        ExecShell "open" "$INSTDIR\current\bin\px_server_tray.exe"
        ReadRegStr $0 HKLM "Software\Pixels\SingleServer" "ConsoleOrigin"
        StrCmp $0 "" console_page_done
        ExecShell "open" "$0/"
    console_page_done:
FunctionEnd

Section "Install"
    ; NSIS is 32-bit; invoke native PowerShell without WOW64 redirection.
    InitPluginsDir
    SetOutPath "$PLUGINSDIR\payload"
    File /r "${PAYLOAD_DIR}\*.*"
    IfFileExists "$ConfigRoot\setup.complete" existing_install fresh_install
    fresh_install:
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\payload\stage_setup.ps1" -PackageRoot "$PLUGINSDIR\payload" -ExpectedManifestSha256 "${MANIFEST_SHA256}" -ConfigRoot "$ConfigRoot" -DataRoot "$DataRoot" -InstallRoot "$INSTDIR"'
    Goto install_result
    existing_install:
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\payload\install.ps1" -PackageRoot "$PLUGINSDIR\payload" -ExpectedManifestSha256 "${MANIFEST_SHA256}" -ConfigRoot "$ConfigRoot" -DataRoot "$DataRoot" -InstallRoot "$INSTDIR" $RecoveryArgument'
    install_result:
    Pop $0
    Pop $InstallOutput
    ClearErrors
    FileOpen $1 "$DataRoot\server-install.log" a
    IfErrors install_log_done
        FileSeek $1 0 END
        FileWrite $1 "Pixels Server ${SUITE_VERSION} exit=$0$\r$\n$InstallOutput$\r$\n"
        FileClose $1
    install_log_done:
    StrCmp $0 "0" install_succeeded
        IfSilent silent_install_failed
        MessageBox MB_ICONSTOP "$(InstallFailed)$\r$\n$InstallOutput"
    silent_install_failed:
        SetErrorLevel 1
        Abort
    install_succeeded:
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "ConfigRoot" "$ConfigRoot"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "DataRoot" "$DataRoot"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "PixelsServerTray" '"$INSTDIR\current\bin\px_server_tray.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "DisplayName" "Pixels Server"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "DisplayVersion" "${SUITE_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "Publisher" "Pixels"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "NoModify" 1
    IfSilent installation_complete mark_console_page
    mark_console_page:
        StrCpy $OpenConsolePage "1"
    installation_complete:
SectionEnd

Section "Uninstall"
    IfFileExists "$INSTDIR\current\uninstall.ps1" installed_uninstall staged_uninstall
    staged_uninstall:
    nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\setup_payload\uninstall.ps1" -InstallRoot "$INSTDIR"'
    Goto uninstall_result
    installed_uninstall:
    nsExec::ExecToLog '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\current\uninstall.ps1" -InstallRoot "$INSTDIR"'
    uninstall_result:
    Pop $0
    StrCmp $0 "0" uninstall_succeeded
        IfSilent silent_uninstall_failed
        MessageBox MB_ICONSTOP "$(UninstallFailed)"
    silent_uninstall_failed:
        SetErrorLevel 1
        Abort
    uninstall_succeeded:
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer"
    DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "PixelsServerTray"
    DeleteRegKey HKLM "Software\Pixels\SingleServer"
SectionEnd
