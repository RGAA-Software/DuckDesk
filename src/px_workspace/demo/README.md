# Minimal classic DuiLib workspace UI probe

Windows-only ordinary window, not a shell replacement. Does not create users,
connect RDP, hide Explorer, register an AppBar or launch the represented applications.
The buttons only update a status label. The independent target does not link Render or Qt.

## Reproduce

Validated on repository `93c5cca09` plus this change, MSVC 19.51, Windows x64.
Dependency: vcpkg baseline `b216ddff25a1f432870e6c340ce79357049ef86e`,
`duilib:x64-windows-static-release` version `2024-12-23#1`, upstream
`502ac62be82c2bc33cf0e8635782fb370c68b1e7` plus the registry's four port patches.
No project-authored third-party source changes. Reuse the existing vcpkg checkout;
do not update its baseline or other installed packages just to run the demo.

```powershell
C:/source/vcpkg/vcpkg.exe install duilib:x64-windows-static-release
scripts_build/build_cpp_workspace_demo.bat
ctest --test-dir build_workspace_demo --output-on-failure
powershell -NoProfile -File scripts_build/publish_workspace_demo.ps1
build_official/dist/workspace_ui_demo/workspace_ui_demo.exe
```

Set `VCPKG_ROOT` before the build script if the dependency checkout is elsewhere.
The script snapshots that value before Visual Studio changes its environment.
The standalone Ninja build uses static DuiLib and static CRT; examples are disabled
by the official port. Published runtime files are the EXE, XML and dependency license.
The publisher refuses copy failures/in-use files rather than reporting partial success.

## Checks and limitations

`--self-test` creates/closes eight windows and activates all three content buttons
and the close button through DuiLib notifications. CTest repeats this at the default
DPI and at layout overrides `--scale=150` / `--scale=200`, including font-size assertions.
Overrides test our XML layout/font scaling; they do NOT change Windows display scaling
or establish real per-monitor DPI transition acceptance. No global display setting changes.
The scaling adapter covers only the fixed numeric XML attributes used in this demo,
not an arbitrary DuiLib skin. Live DPI changes reload the demo layout and reset its status.

The classic library requires an explicit Font id. Windows/GDI+ headers are included
before UIlib to avoid the upstream global namespace interaction with C++23 std::byte.
DuiLib retains its own control tree/window dispatch ABI; project code owns the window
with unique_ptr and unregisters notifications before destruction, without self-delete
or asynchronous raw-pointer captures. Recovery and Service lifecycle are not implemented.

The current UI uses text buttons, not production application icons. Keyboard accessibility,
actual mouse hit-testing at changed OS DPI, minimized/tray applications, real Server sessions,
and all workspace fault recovery remain separate acceptance tasks.
