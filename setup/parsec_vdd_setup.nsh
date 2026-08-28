; Parsec VDD install, legacy migration, and ownership-aware uninstall helpers.

Function InstallParsecVddDriver
    IfFileExists "$INSTDIR\parsec_vdd\nefconw.exe" +2 0
        Goto parsec_vdd_install_failed
    IfFileExists "$INSTDIR\parsec_vdd\driver\mm.inf" +2 0
        Goto parsec_vdd_install_failed

    SetOutPath "$PLUGINSDIR"
    File /oname=verify_parsec_vdd_package.ps1 "verify_parsec_vdd_package.ps1"
    ${DisableX64FSRedirection}
    nsExec::ExecToStack '"$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\verify_parsec_vdd_package.ps1" -PackageRoot "$INSTDIR\parsec_vdd"'
    ${EnableX64FSRedirection}
    Pop $R3
    Pop $R4
    DetailPrint "$R4"
    StrCmp $R3 "0" parsec_vdd_package_verified
        Goto parsec_vdd_install_failed

parsec_vdd_package_verified:
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'Parsec Virtual Display Adapter' -and $$_.Status -eq 'OK' }); if ($$device.Count -gt 0) { exit 0 }; exit 1"`
    Pop $R3
    Pop $R4
    StrCmp $R3 "0" parsec_vdd_install_already_present

    DetailPrint "Installing Microsoft-signed Parsec VDD 0.45..."
    SetOutPath "$INSTDIR\parsec_vdd"
    nsExec::ExecToLog '"$INSTDIR\parsec_vdd\nefconw.exe" --create-device-node --class-name Display --class-guid "4D36E968-E325-11CE-BFC1-08002BE10318" --hardware-id Root\Parsec\VDA'
    nsExec::ExecToStack '"$INSTDIR\parsec_vdd\nefconw.exe" --install-driver --inf-path "$INSTDIR\parsec_vdd\driver\mm.inf"'
    Pop $R0
    Pop $R1
    DetailPrint "Parsec VDD installer exit code: $R0"
    DetailPrint "$R1"

    StrCpy $R2 0
parsec_vdd_install_verify:
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'Parsec Virtual Display Adapter' -and $$_.Status -eq 'OK' }); if ($$device.Count -gt 0) { exit 0 }; exit 1"`
    Pop $R3
    Pop $R4
    StrCmp $R3 "0" parsec_vdd_install_verified
    IntOp $R2 $R2 + 1
    StrCmp $R2 "60" parsec_vdd_install_failed
    Sleep 500
    Goto parsec_vdd_install_verify

parsec_vdd_install_verified:
    WriteRegDWORD HKLM "Software\Pixels\VirtualDisplay" "ParsecVddOwned" 1
    DetailPrint "Parsec VDD is installed and owned by Pixels."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

parsec_vdd_install_already_present:
    DetailPrint "A healthy Parsec VDD already exists; preserving its external ownership."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

parsec_vdd_install_failed:
    DetailPrint "Parsec VDD installation or package verification failed."
    SetOutPath "$INSTDIR"
    Push "1"
FunctionEnd

Function CleanupLegacyUsbMmIddDriver
    ; Only clean the legacy driver when an older Pixels installation left its
    ; product-owned payload. A fresh install must not remove another product's
    ; independently installed Amyuni driver.
    IfFileExists "$INSTDIR\usbmmidd_v2\usbmmIdd.inf" 0 legacy_usbmmidd_cleanup_not_owned
    SetOutPath "$PLUGINSDIR"
    File /oname=cleanup_legacy_usbmmidd_driver.ps1 "cleanup_legacy_usbmmidd_driver.ps1"
    ${DisableX64FSRedirection}
    nsExec::ExecToStack '"$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\cleanup_legacy_usbmmidd_driver.ps1"'
    ${EnableX64FSRedirection}
    Pop $R3
    Pop $R4
    DetailPrint "$R4"
    StrCmp $R3 "0" legacy_usbmmidd_cleanup_success
        Push "1"
        Return

legacy_usbmmidd_cleanup_success:
    RMDir /r "$INSTDIR\usbmmidd_v2"
    DetailPrint "Legacy Pixels USBMMIDD payload and driver were removed."

legacy_usbmmidd_cleanup_not_owned:
    SetOutPath "$INSTDIR"
    Push "0"
FunctionEnd

Function un.UninstallParsecVddDriver
    ReadRegDWORD $R0 HKLM "Software\Pixels\VirtualDisplay" "ParsecVddOwned"
    StrCmp $R0 "1" 0 parsec_vdd_uninstall_not_owned

    IfFileExists "$INSTDIR\parsec_vdd\nefconw.exe" +2 0
        Goto parsec_vdd_uninstall_failed
    DetailPrint "Removing Pixels-owned Parsec VDD..."
    nsExec::ExecToLog 'taskkill /F /T /IM px_display.exe'
    SetOutPath "$INSTDIR\parsec_vdd"
    nsExec::ExecToLog '"$INSTDIR\parsec_vdd\nefconw.exe" --remove-device-node --hardware-id Root\Parsec\VDA --class-guid "4D36E968-E325-11CE-BFC1-08002BE10318"'
    nsExec::ExecToStack '"$INSTDIR\parsec_vdd\nefconw.exe" --uninstall-driver --inf-path "$INSTDIR\parsec_vdd\driver\mm.inf"'
    Pop $R1
    Pop $R2
    DetailPrint "$R2"

    StrCpy $R3 0
parsec_vdd_uninstall_verify:
    nsExec::ExecToStack `powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$$device = @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $$_.FriendlyName -eq 'Parsec Virtual Display Adapter' }); if ($$device.Count -eq 0) { exit 0 }; exit 1"`
    Pop $R1
    Pop $R2
    StrCmp $R1 "0" parsec_vdd_uninstall_verified
    IntOp $R3 $R3 + 1
    StrCmp $R3 "60" parsec_vdd_uninstall_failed
    Sleep 500
    Goto parsec_vdd_uninstall_verify

parsec_vdd_uninstall_verified:
    DeleteRegValue HKLM "Software\Pixels\VirtualDisplay" "ParsecVddOwned"
    DetailPrint "Pixels-owned Parsec VDD was removed."
    SetOutPath "$INSTDIR"
    Push "0"
    Return

parsec_vdd_uninstall_not_owned:
    DetailPrint "Parsec VDD is externally owned; preserving it."
    Push "0"
    Return

parsec_vdd_uninstall_failed:
    DetailPrint "Pixels-owned Parsec VDD is still present."
    SetOutPath "$INSTDIR"
    Push "1"
FunctionEnd
