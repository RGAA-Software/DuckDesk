Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

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
Var ConfigEdit
Var DataEdit

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom ConfigPage ConfigLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString RequiresX64 ${LANG_ENGLISH} "Pixels Server requires Windows x86_64."
LangString RequiresX64 ${LANG_SIMPCHINESE} "Pixels Server 需要 Windows x86_64。"
LangString ConfigIntro ${LANG_ENGLISH} "Choose where Server will keep its private configuration and data. PostgreSQL is installed separately."
LangString ConfigIntro ${LANG_SIMPCHINESE} "选择服务端私有配置及数据的存放位置。PostgreSQL 需单独安装。"
LangString ConfigDirectory ${LANG_ENGLISH} "Private configuration directory:"
LangString ConfigDirectory ${LANG_SIMPCHINESE} "私有配置目录："
LangString DataDirectory ${LANG_ENGLISH} "Persistent data directory:"
LangString DataDirectory ${LANG_SIMPCHINESE} "持久数据目录："
LangString SetupReady ${LANG_ENGLISH} "Installation is ready. Open http://127.0.0.1:4700/ on this machine to finish first-time setup."
LangString SetupReady ${LANG_SIMPCHINESE} "安装已就绪。请在本机打开 http://127.0.0.1:4700/ 完成首次配置。"
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
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Function ConfigPage
    nsDialogs::Create 1018
    Pop $0
    ${NSD_CreateLabel} 0 0 100% 28u "$(ConfigIntro)"
    Pop $0
    ${NSD_CreateLabel} 0 35u 100% 12u "$(ConfigDirectory)"
    Pop $0
    ${NSD_CreateText} 0 49u 100% 13u "$ConfigRoot"
    Pop $ConfigEdit
    ${NSD_CreateLabel} 0 70u 100% 12u "$(DataDirectory)"
    Pop $0
    ${NSD_CreateText} 0 84u 100% 13u "$DataRoot"
    Pop $DataEdit
    nsDialogs::Show
FunctionEnd

Function ConfigLeave
    ${NSD_GetText} $ConfigEdit $ConfigRoot
    ${NSD_GetText} $DataEdit $DataRoot
FunctionEnd

Section "Install"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR\payload"
    File /r "${PAYLOAD_DIR}\*.*"
    IfFileExists "$ConfigRoot\setup.complete" existing_install fresh_install
    fresh_install:
    nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\payload\stage_setup.ps1" -PackageRoot "$PLUGINSDIR\payload" -ExpectedManifestSha256 "${MANIFEST_SHA256}" -ConfigRoot "$ConfigRoot" -DataRoot "$DataRoot" -InstallRoot "$INSTDIR"'
    Goto install_result
    existing_install:
    nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\payload\install.ps1" -PackageRoot "$PLUGINSDIR\payload" -ExpectedManifestSha256 "${MANIFEST_SHA256}" -ConfigRoot "$ConfigRoot" -DataRoot "$DataRoot" -InstallRoot "$INSTDIR"'
    install_result:
    Pop $0
    StrCmp $0 "0" +3
        MessageBox MB_ICONSTOP "$(InstallFailed)"
        Abort
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "ConfigRoot" "$ConfigRoot"
    WriteRegStr HKLM "Software\Pixels\SingleServer" "DataRoot" "$DataRoot"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "DisplayName" "Pixels Server"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "DisplayVersion" "${SUITE_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "Publisher" "Pixels"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer" "NoModify" 1
    IfSilent installation_complete show_setup_link
    show_setup_link:
    IfFileExists "$ConfigRoot\backup.json" installation_complete
        MessageBox MB_ICONINFORMATION "$(SetupReady)"
    installation_complete:
SectionEnd

Section "Uninstall"
    IfFileExists "$INSTDIR\current\uninstall.ps1" installed_uninstall staged_uninstall
    staged_uninstall:
    nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\setup_payload\uninstall.ps1" -InstallRoot "$INSTDIR"'
    Goto uninstall_result
    installed_uninstall:
    nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\current\uninstall.ps1" -InstallRoot "$INSTDIR"'
    uninstall_result:
    Pop $0
    StrCmp $0 "0" +3
        MessageBox MB_ICONSTOP "$(UninstallFailed)"
        Abort
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PixelsSingleServer"
    DeleteRegKey HKLM "Software\Pixels\SingleServer"
SectionEnd
