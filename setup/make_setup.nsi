;--------------------------------
; Modern UI
Unicode true

!include "MUI2.nsh"
!include "x64.nsh"
!include "StrFunc.nsh"

!ifndef PRODUCT_ID
    !error "PRODUCT_ID is required"
!endif
!ifndef PRODUCT_VERSION
    !error "PRODUCT_VERSION is required"
!endif
!ifndef PRODUCT_VERSION_CODE
    !error "PRODUCT_VERSION_CODE is required"
!endif
!ifndef DISTRIBUTION
    !error "DISTRIBUTION is required"
!endif
!if "${DISTRIBUTION}" != "official"
!if "${DISTRIBUTION}" != "customer"
!if "${DISTRIBUTION}" != "oem"
    !error "DISTRIBUTION must be official, customer, or oem"
!endif
!endif
!endif
!ifndef COMPANY
    !error "COMPANY is required"
!endif
!ifndef PUBLISHER_NAME
    !error "PUBLISHER_NAME is required"
!endif
!ifndef RELEASE_NAMESPACE
    !error "RELEASE_NAMESPACE is required"
!endif
!ifndef OEM_ID
    !error "OEM_ID is required, using an empty value for Pixels distributions"
!endif
!if "${DISTRIBUTION}" == "official"
!if "${COMPANY}" != "Pixels"
    !error "Official COMPANY must be Pixels"
!endif
!if "${RELEASE_NAMESPACE}" != "pixels.official"
    !error "Official RELEASE_NAMESPACE must be pixels.official"
!endif
!if "${OEM_ID}" != ""
    !error "Official OEM_ID must be empty"
!endif
!else if "${DISTRIBUTION}" == "customer"
!if "${COMPANY}" != "Pixels"
    !error "Customer COMPANY must be Pixels"
!endif
!if "${RELEASE_NAMESPACE}" != "pixels.customer"
    !error "Customer RELEASE_NAMESPACE must be pixels.customer"
!endif
!if "${OEM_ID}" != ""
    !error "Customer OEM_ID must be empty"
!endif
!else
!if "${COMPANY}" == "Pixels"
    !error "OEM COMPANY must not impersonate Pixels"
!endif
!if "${OEM_ID}" == ""
    !error "OEM_ID is required for OEM installers"
!endif
!if "${RELEASE_NAMESPACE}" != "oem.${OEM_ID}"
    !error "OEM RELEASE_NAMESPACE must exactly match oem.OEM_ID"
!endif
!ifndef OEM_PRODUCT_NAME
    !error "OEM_PRODUCT_NAME is required"
!endif
!ifndef OEM_INSTALL_DIRECTORY_NAME
    !error "OEM_INSTALL_DIRECTORY_NAME is required"
!endif
!ifndef OEM_UNINSTALL_KEY
    !error "OEM_UNINSTALL_KEY is required"
!endif
!ifndef OEM_INSTALLER_BASENAME
    !error "OEM_INSTALLER_BASENAME is required"
!endif
!ifndef OEM_ICON
    !error "OEM_ICON is required"
!endif
!endif

!if "${PRODUCT_ID}" == "cloud_node"
    !define PRODUCT_MARKER "cloud_node"
    !define OTHER_PRODUCT_ONE_KEY "PixelsClient"
    !define OTHER_PRODUCT_TWO_KEY "PixelsRemote"
    !define HAS_HOST 1
!else if "${PRODUCT_ID}" == "client"
    !define PRODUCT_MARKER "client"
    !define OTHER_PRODUCT_ONE_KEY "PixelsCloudNode"
    !define OTHER_PRODUCT_TWO_KEY "PixelsRemote"
    !define HAS_HOST 0
!else if "${PRODUCT_ID}" == "remote"
    !define PRODUCT_MARKER "remote"
    !define OTHER_PRODUCT_ONE_KEY "PixelsCloudNode"
    !define OTHER_PRODUCT_TWO_KEY "PixelsClient"
    !define HAS_HOST 1
!else
    !error "PRODUCT_ID must be cloud_node, client, or remote"
!endif

!if "${DISTRIBUTION}" == "oem"
    !define PRODUCT_NAME "${OEM_PRODUCT_NAME}"
    !define INSTALLER_BASENAME "${OEM_INSTALLER_BASENAME}"
    !define INSTALL_DIR "$PROGRAMFILES64\${OEM_INSTALL_DIRECTORY_NAME}"
    !define UNINSTALL_KEY "${OEM_UNINSTALL_KEY}"
    !define PRODUCT_ICON "${OEM_ICON}"
!else if "${PRODUCT_ID}" == "cloud_node"
    !define PRODUCT_NAME "Pixels Cloud Node"
    !define INSTALLER_BASENAME "PixelsCloudNode"
    !define INSTALL_DIR "$PROGRAMFILES64\Pixels Cloud Node"
    !define UNINSTALL_KEY "PixelsCloudNode"
    !define PRODUCT_ICON "..\src\px_panel\icon.ico"
!else if "${PRODUCT_ID}" == "client"
    !define PRODUCT_NAME "Pixels Client"
    !define INSTALLER_BASENAME "PixelsClient"
    !define INSTALL_DIR "$PROGRAMFILES64\Pixels Client"
    !define UNINSTALL_KEY "PixelsClient"
    !define PRODUCT_ICON "..\src\px_panel\icon.ico"
!else
    !define PRODUCT_NAME "Pixels Remote"
    !define INSTALLER_BASENAME "PixelsRemote"
    !define INSTALL_DIR "$PROGRAMFILES64\Pixels Remote"
    !define UNINSTALL_KEY "PixelsRemote"
    !define PRODUCT_ICON "..\src\px_panel\icon.ico"
!endif

!if ${HAS_HOST} == 1
    ${StrStr}
    !include "parsec_vdd_setup.nsh"
!endif

RequestExecutionLevel admin

;--------------------------------
; App Info
!define APPNAME "px_panel"
!ifndef OUTPUT_DIR
    !define OUTPUT_DIR "."
!endif

OutFile "${OUTPUT_DIR}\${INSTALLER_BASENAME}_${DISTRIBUTION}_${PRODUCT_VERSION}_Setup.exe"

InstallDir "${INSTALL_DIR}"

Name "${PRODUCT_NAME}"

VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey /LANG=1033 "CompanyName" "${COMPANY}"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) ${COMPANY}"

;--------------------------------
!define MUI_ICON "${PRODUCT_ICON}"
!define MUI_UNICON "${PRODUCT_ICON}"

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
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString MSG_CONFLICT ${LANG_ENGLISH} "${PRODUCT_NAME} cannot be installed while $R9 is present.$\r$\nUninstall the other Pixels product first, then run this setup again."
LangString MSG_CONFLICT ${LANG_SIMPCHINESE} "检测到 $R9，无法安装 ${PRODUCT_NAME}。$\r$\n请先卸载其他 Pixels 产品，再重新运行安装程序。"
LangString MSG_LEGACY_CONFLICT ${LANG_ENGLISH} "An old or unowned Pixels installation blocks ${PRODUCT_NAME} setup.$\r$\n$\r$\nDetected item:$\r$\n$R9$\r$\n$\r$\nUninstall or remove this exact installation before running setup again."
LangString MSG_LEGACY_CONFLICT ${LANG_SIMPCHINESE} "旧版或无法确认归属的 Pixels 安装阻止安装 ${PRODUCT_NAME}。$\r$\n$\r$\n检测到的项目：$\r$\n$R9$\r$\n$\r$\n请先卸载或移除此确切安装项目，再重新运行安装程序。"
LangString MSG_REPLACE_FAILED ${LANG_ENGLISH} "The existing ${PRODUCT_NAME} files could not be replaced. Close any process using $INSTDIR and run setup again."
LangString MSG_REPLACE_FAILED ${LANG_SIMPCHINESE} "无法覆盖现有 ${PRODUCT_NAME} 文件。请关闭正在使用 $INSTDIR 的程序后重新运行安装程序。"
LangString MSG_INSTALL_BUSY ${LANG_ENGLISH} "Another Pixels installation, upgrade, rollback, or uninstall is already running. Wait for it to finish and try again."
LangString MSG_INSTALL_BUSY ${LANG_SIMPCHINESE} "另一个 Pixels 安装、升级、回滚或卸载任务正在运行。请等待其完成后重试。"

;--------------------------------
; Sections
Section "Install required files" SecMain

    ; Mutating work starts only after the user has confirmed installation.
    ; This keeps cancelling the welcome/directory pages side-effect free.
!if ${HAS_HOST} == 1
    Call StopServiceForUpgrade
!endif
    Call KillProcesses

    ; A same-Edition install is a complete replacement. Runtime state lives
    ; outside the installation root, so removing the old tree prevents files
    ; retired by the new version from surviving an upgrade or same-version
    ; covering install.
    SetOutPath "$TEMP"
    RMDir /r "$INSTDIR"
    IfFileExists "$INSTDIR\*" replace_failed 0
    CreateDirectory "$INSTDIR"
    SetOutPath "$INSTDIR"
    Goto replace_ready
replace_failed:
    IfSilent +2
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "$(MSG_REPLACE_FAILED)"
    SetErrorLevel 1603
    Abort "$(MSG_REPLACE_FAILED)"
replace_ready:

    ; 1. Install the already verified product payload directly. The release
    ;    builder verifies owned PE inventory and hashes before makensis reads this directory.
    SetOutPath "$INSTDIR"
    File /r "${OUTPUT_DIR}\app\*"

!if ${HAS_HOST} == 1
    ; 2. Install the Microsoft-signed Parsec virtual display driver.
    Call InstallParsecVddDriver
    Pop $R0
    StrCmp $R0 "0" parsec_vdd_install_ok
        IfSilent +2
            MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "Failed to install the Parsec virtual display driver. Setup cannot continue."
        SetErrorLevel 1603
        Abort "Parsec virtual display driver installation failed"
parsec_vdd_install_ok:

    ; 3. Install ViGEm joystick driver silently
    ExecWait '"$INSTDIR\px_joystick.exe" /S'

    ; 4. Keep the last successfully installed full installer outside
    ; the replaceable installation tree. Restrict the entire update cache to
    ; SYSTEM and local administrators before the Service can consume it.
    ReadEnvStr $R2 "PUBLIC"
    StrCmp $R2 "" update_cache_public_missing
    CreateDirectory "$R2\Pixels"
    CreateDirectory "$R2\Pixels\px_data"
    CreateDirectory "$R2\Pixels\px_data\updates"
    CreateDirectory "$R2\Pixels\px_data\updates\rollback"
    ; Secure the cache root first, then make all existing descendants inherit
    ; that ACL. Applying directory-only (OI)(CI) grants recursively can leave
    ; an existing file with an empty DACL, which prevents the next installer
    ; (including SYSTEM) from replacing the cached rollback package.
    nsExec::ExecToStack 'icacls "$R2\Pixels\px_data\updates" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" /C'
    Pop $R0
    Pop $R1
    DetailPrint "$R1"
    StrCmp $R0 "0" update_cache_root_acl_ready
        SetErrorLevel 1603
        Abort "Failed to protect the update cache: $R1"
update_cache_root_acl_ready:
    nsExec::ExecToStack 'icacls "$R2\Pixels\px_data\updates\*" /inheritance:e /T /C'
    Pop $R0
    Pop $R1
    DetailPrint "$R1"
    StrCmp $R0 "0" update_cache_child_inheritance_ready
        SetErrorLevel 1603
        Abort "Failed to restore update cache inheritance: $R1"
update_cache_child_inheritance_ready:
    nsExec::ExecToStack 'icacls "$R2\Pixels\px_data\updates\*" /reset /T /C'
    Pop $R0
    Pop $R1
    DetailPrint "$R1"
    StrCmp $R0 "0" update_cache_acl_ready
        SetErrorLevel 1603
        Abort "Failed to normalize update cache permissions: $R1"
update_cache_acl_ready:
    ClearErrors
!if "${DISTRIBUTION}" == "oem"
    CopyFiles /SILENT "$EXEPATH" "$R2\Pixels\px_data\updates\rollback\${PRODUCT_ID}-${DISTRIBUTION}-${OEM_ID}-current.exe"
!else
    CopyFiles /SILENT "$EXEPATH" "$R2\Pixels\px_data\updates\rollback\${PRODUCT_ID}-${DISTRIBUTION}-pixels-current.exe"
!endif
    IfErrors update_cache_failed update_cache_ready
update_cache_public_missing:
        SetErrorLevel 1603
        Abort "Windows PUBLIC directory is unavailable"
update_cache_failed:
        SetErrorLevel 1603
        Abort "Failed to publish the rollback installer"
update_cache_ready:

    ; 5. Register or update the Windows service only after all runtime files
    ; have been published. The service manager also starts the service.
    Call InstallAndStartService
!endif

    ; 6. Create shortcuts
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\${APPNAME}.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

    ; 7. Write uninstall registry info. Remove the former 32-bit-view key so
    ; packages produced before the registry-view fix cannot leave a ghost
    ; installation behind after upgrade.
    SetRegView 32
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}"
    SetRegView 64
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APPNAME}.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "Publisher" "${PUBLISHER_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "Distribution" "${DISTRIBUTION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "ReleaseNamespace" "${RELEASE_NAMESPACE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "OemId" "${OEM_ID}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "NoRepair" 1

    WriteRegStr HKLM "Software\Pixels\ProductOwner" "ProductId" "${PRODUCT_ID}"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "Distribution" "${DISTRIBUTION}"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "ReleaseNamespace" "${RELEASE_NAMESPACE}"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "OemId" "${OEM_ID}"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "UninstallKey" "${UNINSTALL_KEY}"
    WriteRegStr HKLM "Software\Pixels\ProductOwner" "DisplayName" "${PRODUCT_NAME}"

    FileOpen $R0 "$INSTDIR\product-edition.txt" w
    FileWrite $R0 "${PRODUCT_MARKER}$\r$\n${PRODUCT_VERSION}$\r$\n${COMPANY}$\r$\n${RELEASE_NAMESPACE}$\r$\n${OEM_ID}$\r$\n"
    FileClose $R0

    ; Set the app to run as administrator
    WriteRegStr HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\${APPNAME}.exe" "RUNASADMIN"

    ; Write the uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    Call LaunchLink
SectionEnd

;--------------------------------
; Uninstaller
Section "Uninstall"
!if ${HAS_HOST} == 1
    Call un.StopServiceForRemoval
!endif
    Call un.KillProcesses

!if ${HAS_HOST} == 1
    ; Remove Parsec VDD only when this product installed/owns the device.
    Call un.UninstallParsecVddDriver
    Pop $R0
    StrCmp $R0 "0" parsec_vdd_uninstall_ok
        IfSilent +2
            MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "Failed to remove the Parsec virtual display driver. Uninstall cannot continue."
        SetErrorLevel 1603
        Abort "Parsec virtual display driver removal failed"
parsec_vdd_uninstall_ok:
    Call un.DeleteServiceRegistration
!endif

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
    SetRegView 32
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}"
    Call un.ReleaseProductOwner
    SetRegView 64
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}"
!if ${HAS_HOST} == 1
    SetRegView 32
    DeleteRegKey HKLM "Software\Pixels\VirtualDisplay"
    SetRegView 64
    DeleteRegKey HKLM "Software\Pixels\VirtualDisplay"
!endif
    DeleteRegValue HKCU "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" "$INSTDIR\${APPNAME}.exe"

SectionEnd

;--------------------------------
Function .onInit
    Call AcquireInstallerMutationMutex
    ${IfNot} ${RunningX64}
        SetErrorLevel 1633
        Abort "${PRODUCT_NAME} requires 64-bit Windows."
    ${EndIf}
    SetRegView 64
    SetShellVarContext all

    ; A shared ownership record makes every current Official, Customer and OEM
    ; package mutually exclusive even though each OEM has independent uninstall
    ; keys and installation directories.
    Call CheckGlobalProductOwner

    ; Reuse a same-product custom installation directory for upgrades and
    ; covering installs. Accept the old 32-bit uninstall-registry view once,
    ; then normalize it to the 64-bit view during installation.
    Call ResolveExistingInstallDirectory

    ; Product mutual exclusion is checked before any process, file, service or
    ; driver is stopped or changed.
    Call CheckProductMutualExclusion
FunctionEnd

Function un.onInit
    Call un.AcquireInstallerMutationMutex
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Function AcquireInstallerMutationMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\PixelsInstallerMutation") p .r8 ?e'
    Pop $R0
    StrCmp $R8 "0" installer_mutex_failed
    StrCmp $R0 "183" installer_mutex_busy installer_mutex_ready
installer_mutex_failed:
    SetErrorLevel 1603
    Abort "Cannot create the Pixels installer mutation lock."
installer_mutex_busy:
    SetErrorLevel 1618
    Abort "$(MSG_INSTALL_BUSY)"
installer_mutex_ready:
FunctionEnd

Function un.AcquireInstallerMutationMutex
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\PixelsInstallerMutation") p .r8 ?e'
    Pop $R0
    StrCmp $R8 "0" un_installer_mutex_failed
    StrCmp $R0 "183" un_installer_mutex_busy un_installer_mutex_ready
un_installer_mutex_failed:
    SetErrorLevel 1603
    Abort "Cannot create the Pixels installer mutation lock."
un_installer_mutex_busy:
    SetErrorLevel 1618
    Abort "$(MSG_INSTALL_BUSY)"
un_installer_mutex_ready:
FunctionEnd

Function ResolveExistingInstallDirectory
    SetRegView 64
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "InstallLocation"
    ReadRegStr $R1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "Distribution"
    ReadRegStr $R2 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "ReleaseNamespace"
    ReadRegStr $R3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "OemId"
    StrCmp $R0 "" resolve_existing_32 resolve_existing_found
resolve_existing_32:
    SetRegView 32
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "InstallLocation"
    ReadRegStr $R1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "Distribution"
    ReadRegStr $R2 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "ReleaseNamespace"
    ReadRegStr $R3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "OemId"
    SetRegView 64
resolve_existing_found:
    StrCmp $R0 "" resolve_existing_done
    StrCmp $R1 "${DISTRIBUTION}" 0 resolve_existing_legacy
    StrCmp $R2 "${RELEASE_NAMESPACE}" 0 resolve_existing_legacy
    StrCmp $R3 "${OEM_ID}" resolve_existing_distribution_ok resolve_existing_legacy
resolve_existing_distribution_ok:
    IfFileExists "$R0\product-edition.txt" resolve_existing_owned 0
    ; A terminated covering install can remove the old tree before the new
    ; marker and uninstaller are written. Permit the same installer to repair
    ; that state only when the independently stored global ownership record
    ; still identifies this exact product, edition, and installation path.
    ReadRegStr $R4 HKLM "Software\Pixels\ProductOwner" "ProductId"
    ReadRegStr $R5 HKLM "Software\Pixels\ProductOwner" "Distribution"
    ReadRegStr $R6 HKLM "Software\Pixels\ProductOwner" "ReleaseNamespace"
    ReadRegStr $R7 HKLM "Software\Pixels\ProductOwner" "OemId"
    ReadRegStr $0 HKLM "Software\Pixels\ProductOwner" "InstallLocation"
    ReadRegStr $1 HKLM "Software\Pixels\ProductOwner" "UninstallKey"
    StrCmp $R4 "${PRODUCT_ID}" 0 resolve_existing_legacy
    StrCmp $R5 "${DISTRIBUTION}" 0 resolve_existing_legacy
    StrCmp $R6 "${RELEASE_NAMESPACE}" 0 resolve_existing_legacy
    StrCmp $R7 "${OEM_ID}" 0 resolve_existing_legacy
    StrCmp $0 $R0 0 resolve_existing_legacy
    StrCmp $1 "${UNINSTALL_KEY}" 0 resolve_existing_legacy
resolve_existing_owned:
    StrCpy $INSTDIR $R0
    Goto resolve_existing_done
resolve_existing_legacy:
    StrCpy $R9 "${UNINSTALL_KEY}$\r$\n$R0"
    Call AbortLegacyProduct
resolve_existing_done:
FunctionEnd

Function CheckGlobalProductOwner
    SetRegView 64
    ReadRegStr $R0 HKLM "Software\Pixels\ProductOwner" "ProductId"
    StrCmp $R0 "" product_owner_done
    ReadRegStr $R1 HKLM "Software\Pixels\ProductOwner" "Distribution"
    ReadRegStr $R2 HKLM "Software\Pixels\ProductOwner" "ReleaseNamespace"
    ReadRegStr $R3 HKLM "Software\Pixels\ProductOwner" "OemId"
    ReadRegStr $R4 HKLM "Software\Pixels\ProductOwner" "InstallLocation"
    ReadRegStr $R5 HKLM "Software\Pixels\ProductOwner" "UninstallKey"
    ReadRegStr $R6 HKLM "Software\Pixels\ProductOwner" "DisplayName"
    StrCmp $R0 "${PRODUCT_ID}" 0 product_owner_conflict
    StrCmp $R1 "${DISTRIBUTION}" 0 product_owner_conflict
    StrCmp $R2 "${RELEASE_NAMESPACE}" 0 product_owner_conflict
    StrCmp $R3 "${OEM_ID}" 0 product_owner_conflict
    StrCmp $R5 "${UNINSTALL_KEY}" 0 product_owner_conflict
    ReadRegStr $R7 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$R5" "InstallLocation"
    StrCmp $R7 $R4 0 product_owner_conflict
    ; Exact registry ownership is sufficient to enter the later repair check.
    ; The payload marker may legitimately be absent after an interrupted
    ; covering install. ResolveExistingInstallDirectory independently checks
    ; the same ownership tuple before selecting the damaged directory.
    Goto product_owner_done
product_owner_conflict:
    StrCmp $R6 "" 0 +2
        StrCpy $R6 "unowned product installation"
    StrCpy $R9 "$R6$\r$\n$R4"
    Call AbortConflictingProduct
product_owner_done:
FunctionEnd

Function un.ReleaseProductOwner
    SetRegView 64
    ReadRegStr $R0 HKLM "Software\Pixels\ProductOwner" "UninstallKey"
    ReadRegStr $R1 HKLM "Software\Pixels\ProductOwner" "InstallLocation"
    StrCmp $R0 "${UNINSTALL_KEY}" 0 release_product_owner_done
    StrCmp $R1 "$INSTDIR" 0 release_product_owner_done
    DeleteRegKey HKLM "Software\Pixels\ProductOwner"
release_product_owner_done:
FunctionEnd

Function AbortConflictingProduct
    IfSilent conflict_abort
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "$(MSG_CONFLICT)"
conflict_abort:
    SetErrorLevel 1638
    Abort "$(MSG_CONFLICT)"
FunctionEnd

Function AbortLegacyProduct
    IfSilent legacy_abort
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "$(MSG_LEGACY_CONFLICT)"
legacy_abort:
    SetErrorLevel 1638
    Abort "$(MSG_LEGACY_CONFLICT)"
FunctionEnd

Function CheckProductMutualExclusion
    SetRegView 64
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${OTHER_PRODUCT_ONE_KEY}" "DisplayName"
    StrCmp $R0 "" check_other_one_32
        StrCpy $R9 $R0
        Call AbortConflictingProduct
check_other_one_32:
    SetRegView 32
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${OTHER_PRODUCT_ONE_KEY}" "DisplayName"
    SetRegView 64
    StrCmp $R0 "" check_other_two
        StrCpy $R9 $R0
        Call AbortConflictingProduct
check_other_two:
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${OTHER_PRODUCT_TWO_KEY}" "DisplayName"
    StrCmp $R0 "" check_other_two_32
        StrCpy $R9 $R0
        Call AbortConflictingProduct
check_other_two_32:
    SetRegView 32
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${OTHER_PRODUCT_TWO_KEY}" "DisplayName"
    SetRegView 64
    StrCmp $R0 "" check_legacy_key
        StrCpy $R9 $R0
        Call AbortConflictingProduct
check_legacy_key:
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel" "DisplayName"
    StrCmp $R0 "" check_legacy_key_32
        StrCpy $R9 "$R0$\r$\nHKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel"
        Call AbortLegacyProduct
check_legacy_key_32:
    SetRegView 32
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel" "DisplayName"
    SetRegView 64
    StrCmp $R0 "" check_known_directories
        StrCpy $R9 "$R0$\r$\nHKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Pixels px_panel"
        Call AbortLegacyProduct

check_known_directories:
!if "${PRODUCT_ID}" != "cloud_node"
    IfFileExists "$PROGRAMFILES64\Pixels Cloud Node\product-edition.txt" cloud_node_directory_conflict check_client_directory
cloud_node_directory_conflict:
        StrCpy $R9 "Pixels Cloud Node"
        Call AbortConflictingProduct
check_client_directory:
!endif
!if "${PRODUCT_ID}" != "client"
    IfFileExists "$PROGRAMFILES64\Pixels Client\product-edition.txt" client_directory_conflict check_remote_directory
client_directory_conflict:
        StrCpy $R9 "Pixels Client"
        Call AbortConflictingProduct
check_remote_directory:
!endif
!if "${PRODUCT_ID}" != "remote"
    IfFileExists "$PROGRAMFILES64\Pixels Remote\product-edition.txt" remote_directory_conflict check_legacy_directory
remote_directory_conflict:
        StrCpy $R9 "Pixels Remote"
        Call AbortConflictingProduct
check_legacy_directory:
!endif
    IfFileExists "$PROGRAMFILES64\PixelsRender\*" 0 check_service
        StrCpy $R9 "$PROGRAMFILES64\PixelsRender"
        Call AbortLegacyProduct

check_service:
    ReadRegStr $R1 HKLM "SYSTEM\CurrentControlSet\Services\px_service" "ImagePath"
    StrCmp $R1 "" mutual_check_done
!if ${HAS_HOST} == 0
        StrCpy $R9 "px_service$\r$\n$R1"
        Call AbortLegacyProduct
!else
    SetRegView 64
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "InstallLocation"
    StrCmp $R0 "" check_service_install_32
    Goto check_service_install_found
check_service_install_32:
    SetRegView 32
    ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}" "InstallLocation"
    SetRegView 64
check_service_install_found:
    StrCmp $R0 "" service_conflict
    ${StrStr} $R2 $R1 "$R0\px_service.exe"
    StrCmp $R2 "" service_conflict mutual_check_done
service_conflict:
        StrCpy $R9 "px_service$\r$\n$R1"
        Call AbortLegacyProduct
!endif
mutual_check_done:
FunctionEnd

Function LaunchLink
    IfSilent launch_done
    ExecShell "" "$INSTDIR\${APPNAME}.exe"
launch_done:
FunctionEnd

!if ${HAS_HOST} == 1
Function StopServiceForUpgrade
    ; net stop waits for the service process to release files. A missing service
    ; is valid during a clean installation.
    nsExec::ExecToLog 'net stop "px_service"'
FunctionEnd

Function InstallAndStartService
    IfFileExists "$INSTDIR\px_service.exe" service_binary_present 0
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "px_service.exe is missing. Setup cannot continue."
        SetErrorLevel 1603
        Abort "px_service.exe is missing"
service_binary_present:
    IfFileExists "$INSTDIR\px_service_manager.exe" service_manager_present 0
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "px_service_manager.exe is missing. Setup cannot continue."
        SetErrorLevel 1603
        Abort "px_service_manager.exe is missing"
service_manager_present:
    DetailPrint "Registering and starting px_service..."
    nsExec::ExecToStack '"$INSTDIR\px_service_manager.exe" install --service-bin "$INSTDIR\px_service.exe"'
    Pop $R0
    Pop $R1
    DetailPrint "$R1"
    StrCmp $R0 "0" service_install_ok
        MessageBox MB_OK|MB_ICONSTOP|MB_TOPMOST "Failed to register or start px_service. Setup cannot continue."
        SetErrorLevel 1603
        Abort "px_service installation failed: $R1"
service_install_ok:
FunctionEnd

Function un.StopServiceForRemoval
    nsExec::ExecToLog 'net stop "px_service"'
FunctionEnd

Function un.DeleteServiceRegistration
    nsExec::ExecToLog 'sc delete "px_service"'
FunctionEnd
!endif


Function KillProcesses
    ; Order: kill the guardian (UserProxy) first, then the guard processes;
    ; SysInfo needs only one kill
    nsExec::ExecToLog 'taskkill /F /T /IM px_function.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_client.exe'
!if ${HAS_HOST} == 1
    nsExec::ExecToLog 'taskkill /F /T /IM px_render.exe'
!endif
    nsExec::ExecToLog 'taskkill /F /T /IM px_panel.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_osinfo.exe'
!if ${HAS_HOST} == 1
    nsExec::ExecToLog 'taskkill /F /T /IM px_display.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service_manager.exe'
!endif
FunctionEnd

Function un.KillProcesses
    nsExec::ExecToLog 'taskkill /F /T /IM px_function.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_client.exe'
!if ${HAS_HOST} == 1
    nsExec::ExecToLog 'taskkill /F /T /IM px_render.exe'
!endif
    nsExec::ExecToLog 'taskkill /F /T /IM px_panel.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_osinfo.exe'
!if ${HAS_HOST} == 1
    nsExec::ExecToLog 'taskkill /F /T /IM px_display.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service.exe'
    nsExec::ExecToLog 'taskkill /F /T /IM px_service_manager.exe'
!endif
FunctionEnd
