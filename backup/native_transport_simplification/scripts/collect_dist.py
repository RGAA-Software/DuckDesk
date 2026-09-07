#!/usr/bin/env python3
"""Collect build artifacts into a unified dist/ folder.

Copies directly from each target's native build directory.
No intermediate CMake copy steps required.
"""

import argparse
import filecmp
import os
import shutil
import sys

# Directories inside src/px_deps/ that we keep as-is
KEEP_DIRS = {
    "certs", "resources", "translations", "www", "package",
    "generic", "iconengines", "imageformats", "networkinformation",
    "platforms", "styles", "tls",
}

# Build-system dirs to skip when scanning
SKIP_DIRS = {"CMakeFiles", "deps", "px_client_web", "web"}

# File extensions to skip
SKIP_EXTS = {".pdb", ".ilk", ".lib", ".exp", ".obj", ".res", ".manifest", ".cmake"}

# Stale bundled FFmpeg DLLs that should not be copied now that FFmpeg is statically linked via vcpkg
SKIP_NAMES = {
    "avcodec-61.dll", "avdevice-61.dll", "avfilter-10.dll", "avformat-61.dll",
    "avutil-59.dll", "postproc-58.dll", "swresample-5.dll", "swscale-8.dll",
    # Retired Render modules may remain in an incremental build tree. They must
    # never be collected into a production distribution.
    "mock_video_stream.dll", "obj_detector.dll", "frame_debugger.dll",
    "media_recorder.dll",
    "live_pusher.dll",
    "frame_resizer.dll",
    "frame_carrier.dll",
    "enc_opus.dll",
    "event_replayer.dll",
    "cap_was_audio.dll",
    "clipboard.dll",
    "joystick.dll", "ft.dll", "voice_call.dll",
    "cap_dda.dll", "cap_gdi.dll",
    "enc_ffmpeg.dll", "enc_amf.dll", "enc_nvenc.dll",
    "net_ws.dll", "net_udp.dll", "net_relay.dll",
    "net_rtc.dll", "net_rtc_local.dll",
}

# Test executable prefix
TEST_PREFIX = "test_"

# Product executables we want to keep (basename match)
PRODUCT_EXES = {
    "px_panel.exe",
    "px_client.exe",
    "px_render.exe",
    "px_service.exe",
    "px_service_manager.exe",
    "px_display.exe",
    "px_osinfo.exe",
    "px_function.exe",
    "px_uninstall.exe",
    "px_joystick.exe",
    "px_gh_injector.exe",
    "px_gh_address.exe",
    "vc_redist.x64.exe",
}


def should_copy_file(name: str) -> bool:
    if name.lower() in SKIP_NAMES:
        return False
    base, ext = os.path.splitext(name)
    if ext.lower() in SKIP_EXTS:
        return False
    if ext.lower() == ".exe":
        if name.lower().startswith(TEST_PREFIX):
            return False
        return name in PRODUCT_EXES
    return True


def copy_tree(src: str, dst: str):
    """Copy directory tree, skipping unwanted files/dirs."""
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel_root = os.path.relpath(root, src)
        dst_root = os.path.join(dst, rel_root) if rel_root != "." else dst
        os.makedirs(dst_root, exist_ok=True)
        for f in files:
            if should_copy_file(f):
                shutil.copy2(os.path.join(root, f), os.path.join(dst_root, f))


def copy_file(src: str, dst: str):
    if os.path.isfile(src):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.isfile(dst) and filecmp.cmp(src, dst, shallow=False):
            print(f"  = {os.path.relpath(dst, dist_dir)} (already current)")
            return
        try:
            shutil.copy2(src, dst)
        except PermissionError:
            # Python 3.14 uses Windows CopyFile2 for copy2(). CopyFile2 can
            # reject a stopped service's registered executable path even when
            # no process has an open handle; a normal streamed overwrite is
            # accepted by Windows in that case. A real sharing violation still
            # fails when either file is opened below and remains visible.
            with open(src, "rb") as source, open(dst, "wb") as destination:
                shutil.copyfileobj(source, destination, length=1024 * 1024)
            shutil.copystat(src, dst)
        print(f"  + {os.path.relpath(dst, dist_dir)}")
    else:
        print(f"  - missing: {src}")


def main():
    parser = argparse.ArgumentParser(description="Collect build artifacts to dist/")
    parser.add_argument("--build-dir", required=True, help="CMake binary dir")
    parser.add_argument(
        "--dist-dir",
        help="Output directory (defaults to <build-dir>/dist)",
    )
    args = parser.parse_args()

    global dist_dir
    build_dir = os.path.abspath(args.build_dir)
    source_dir = os.path.abspath(os.path.join(build_dir, ".."))
    dist_dir = (
        os.path.abspath(args.dist_dir)
        if args.dist_dir
        else os.path.join(build_dir, "dist")
    )

    # This directory is replaced on every collection. Refuse broad targets so
    # a typo in --dist-dir cannot erase a drive, source tree, or build tree.
    if os.path.dirname(dist_dir) == dist_dir or dist_dir in {source_dir, build_dir}:
        parser.error(f"unsafe --dist-dir target: {dist_dir}")

    if os.path.isdir(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir, exist_ok=True)

    # ------------------------------------------------------------------
    # 1. Base: src/px_deps/ (main exes, Qt DLLs, resources, etc.)
    # ------------------------------------------------------------------
    gamma_ray_dir = os.path.join(build_dir, "src", "px_deps")
    if os.path.isdir(gamma_ray_dir):
        for entry in os.listdir(gamma_ray_dir):
            src_path = os.path.join(gamma_ray_dir, entry)
            dst_path = os.path.join(dist_dir, entry)
            if os.path.isdir(src_path):
                if entry in KEEP_DIRS:
                    copy_tree(src_path, dst_path)
                elif entry.startswith("px_"):
                    # skip submodule build output directories
                    pass
                elif entry not in SKIP_DIRS:
                    copy_tree(src_path, dst_path)
            else:
                if should_copy_file(entry):
                    shutil.copy2(src_path, dst_path)

    # px_client owns the runtime language and Qt material resources used by
    # its floating controller.  Overlay its post-build resource directory so
    # newly added client strings cannot be shadowed by stale px_deps output.
    client_resources_dir = os.path.join(build_dir, "src", "px_client", "resources")
    if os.path.isdir(client_resources_dir):
        copy_tree(client_resources_dir, os.path.join(dist_dir, "resources"))
        print("  + resources/  (from px_client post-build output)")

    # Language JSON is runtime data rather than a compiled Qt resource.  The
    # post-build directory may be stale when only wording changes, so always
    # overlay the authoritative source files for packaging.
    source_language_dir = os.path.join(
        source_dir, "src", "px_panel", "resources", "language")
    if not os.path.isdir(source_language_dir):
        print(f"ERROR: missing runtime language directory: {source_language_dir}", file=sys.stderr)
        sys.exit(1)
    copy_tree(source_language_dir, os.path.join(dist_dir, "resources", "language"))
    print("  + resources/language/  (authoritative source files)")

    frame_carrier_resource = os.path.join(
        source_dir, "src", "px_render", "architecture", "processors",
        "frame_carrier", "resources", "ic_logo_point.png")
    copy_file(
        frame_carrier_resource,
        os.path.join(
            dist_dir, "resources", "render", "frame_carrier",
            "ic_logo_point.png"),
    )

    # ------------------------------------------------------------------
    # 2. Supplementary executables / DLLs from native build dirs
    # ------------------------------------------------------------------
    supplements = [
        ("src/px_client/px_client.exe", "px_client.exe"),
        # SDL2/gflags/fftw3 are now statically linked via x64-windows-static-release
        ("libplacebo-349.dll", "libplacebo-349.dll"),
        ("src/px_render/px_render.exe", "px_render.exe"),
        ("src/px_deps/px_webrtc_client/px_rtc_client.dll", "px_client_rtc.dll"),
        ("src/px_deps/px_voice_call/px_voice_apm.dll", "px_voice_apm.dll"),
    ]
    for rel_src, rel_dst in supplements:
        copy_file(rel_src if os.path.isabs(rel_src) else os.path.join(build_dir, rel_src), os.path.join(dist_dir, rel_dst))

    # px_service is built by Cargo rather than the CMake graph. Always take the
    # current release artifact explicitly; the copy under build/src/px_deps can
    # be left over from an earlier build and has caused ABI/feature skew in
    # isolated deployments.
    copy_file(
        os.path.join(source_dir, "rust_client", "target", "release", "px_service.exe"),
        os.path.join(dist_dir, "px_service.exe"),
    )
    copy_file(
        os.path.join(build_dir, "px_display", "px_display.exe"),
        os.path.join(dist_dir, "px_display.exe"),
    )
    copy_file(
        os.path.join(build_dir, "px_display", "px_display.exe.config"),
        os.path.join(dist_dir, "px_display.exe.config"),
    )

    # CEF runtime is staged beside px_render by its CMake target. Keep the same
    # adjacency in dist so px_render can act as browser/GPU/renderer subprocess.
    cef_runtime_dir = os.path.join(build_dir, "src", "px_render")
    cef_runtime_files = [
        "chrome_elf.dll", "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll",
        "libcef.dll", "libEGL.dll", "libGLESv2.dll", "v8_context_snapshot.bin",
        "vk_swiftshader.dll", "vk_swiftshader_icd.json", "vulkan-1.dll",
        "chrome_100_percent.pak", "chrome_200_percent.pak", "resources.pak", "icudtl.dat",
    ]
    missing_cef = [
        name for name in cef_runtime_files
        if not os.path.isfile(os.path.join(cef_runtime_dir, name))
    ]
    cef_locales = os.path.join(cef_runtime_dir, "locales")
    if missing_cef or not os.path.isfile(os.path.join(cef_locales, "zh-CN.pak")):
        details = ", ".join(missing_cef) if missing_cef else "locales/zh-CN.pak"
        print(f"ERROR: incomplete CEF runtime beside px_render: {details}", file=sys.stderr)
        sys.exit(1)
    for name in cef_runtime_files:
        copy_file(os.path.join(cef_runtime_dir, name), os.path.join(dist_dir, name))
    copy_tree(cef_locales, os.path.join(dist_dir, "locales"))

    # ------------------------------------------------------------------
    # 3. Concrete Render network libraries → beside dist/px_render.exe
    # ------------------------------------------------------------------
    legacy_render_plugins = os.path.join(dist_dir, "deps", "rd_plugins")
    if os.path.isdir(legacy_render_plugins):
        shutil.rmtree(legacy_render_plugins)
        print("  - deps/rd_plugins  (legacy Render plug-in directory)")
    network_libraries = [
        ("network/webrtc/remote/px_render_rtc_remote.dll", "px_render_rtc_remote.dll"),
        ("network/webrtc/local/px_render_rtc.dll", "px_render_rtc.dll"),
    ]
    network_dst = os.path.join(dist_dir, "deps", "network")
    for stale_name in ["net_rtc.dll", "net_rtc_local.dll"]:
        stale_path = os.path.join(dist_dir, stale_name)
        if os.path.isfile(stale_path):
            os.remove(stale_path)
            print(f"  - {stale_name}  (retired WebRTC artifact name)")
    for stale_name in ["net_rtc.dll", "net_rtc_local.dll", "px_render_rtc.dll", "px_render_rtc_remote.dll"]:
        stale_path = os.path.join(network_dst, stale_name)
        if os.path.isfile(stale_path):
            os.remove(stale_path)
            print(f"  - deps/network/{stale_name}  (retired WebRTC delivery location)")
    if os.path.isdir(network_dst) and not os.listdir(network_dst):
        os.rmdir(network_dst)
        print("  - deps/network  (empty retired WebRTC delivery directory)")
    for relative_source, name in network_libraries:
        source = os.path.join(
            build_dir, "src", "px_render", *relative_source.split("/"))
        copy_file(source, os.path.join(dist_dir, name))

    # ------------------------------------------------------------------
    # 4. Retired Client plug-ins and the temporary recording-core DLL
    # ------------------------------------------------------------------
    stale_recording_core = os.path.join(dist_dir, "px_client_recording_core.dll")
    if os.path.isfile(stale_recording_core):
        os.remove(stale_recording_core)
        print("  - px_client_recording_core.dll  (recording core is static)")
    px_plugins_client_dst = os.path.join(dist_dir, "deps", "ct_plugins")
    for stale_name in [
        "clipboard.dll",
        "ft.dll",
        "record.dll",
        "client_clipboard.dll",
        "ft_client.dll",
        "media_record_client.dll",
    ]:
        stale_path = os.path.join(px_plugins_client_dst, stale_name)
        if os.path.isfile(stale_path):
            os.remove(stale_path)
            print(f"  - deps/ct_plugins/{stale_name}  (retired client plug-in)")
    if os.path.isdir(px_plugins_client_dst) and not os.listdir(px_plugins_client_dst):
        os.rmdir(px_plugins_client_dst)

    # ------------------------------------------------------------------
    # 5. Skins  →  dist/deps/theme/
    # ------------------------------------------------------------------
    px_skins_dst = os.path.join(dist_dir, "deps", "theme")
    os.makedirs(px_skins_dst, exist_ok=True)

    # skin_official / skin_opensource DLLs + config, built directly into px_skins/
    # (see src/px_panel/src/skin/{official,opensource}/CMakeLists.txt RUNTIME_OUTPUT_DIRECTORY).
    skins_src = os.path.join(build_dir, "src", "px_deps", "px_skins")
    if os.path.isdir(skins_src):
        for f in os.listdir(skins_src):
            if (f.startswith("skin_") and f.endswith(".dll")) or f == "skin_config.toml":
                copy_file(os.path.join(skins_src, f), os.path.join(px_skins_dst, f))

    # ------------------------------------------------------------------
    # 6. Hook capture
    # ------------------------------------------------------------------
    hook_capture_files = [
        ("src/px_render/hook_capture/win/hk_obs/px_gh.dll", "px_gh.dll"),
        ("src/px_render/hook_capture/win/hk_obs/injector/px_gh_injector.exe", "px_gh_injector.exe"),
        ("src/px_render/hook_capture/win/hk_obs/offsets/px_gh_address.exe", "px_gh_address.exe"),
    ]
    for rel_src, rel_dst in hook_capture_files:
        copy_file(os.path.join(build_dir, rel_src), os.path.join(dist_dir, rel_dst))

    # ------------------------------------------------------------------
    # 7. Joystick (source tree)
    # ------------------------------------------------------------------
    joystick_src = os.path.join(build_dir, "..", "src", "px_deps", "px_controller", "vigem", "driver", "px_joystick.exe")
    if os.path.isfile(joystick_src):
        copy_file(joystick_src, os.path.join(dist_dir, "px_joystick.exe"))

    # ------------------------------------------------------------------
    # 8. Microsoft-signed Parsec virtual display driver (source tree)
    # ------------------------------------------------------------------
    parsec_vdd_src = os.path.join(source_dir, "third_party", "parsec_vdd")
    parsec_vdd_dst = os.path.join(dist_dir, "parsec_vdd")
    parsec_vdd_required = {
        "nefconw.exe",
        "SOURCE.md",
        "SHA256SUMS.txt",
        os.path.join("driver", "mm.cat"),
        os.path.join("driver", "mm.dll"),
        os.path.join("driver", "mm.inf"),
    }
    missing_parsec_vdd = sorted(
        rel for rel in parsec_vdd_required
        if not os.path.isfile(os.path.join(parsec_vdd_src, rel))
    )
    if missing_parsec_vdd:
        print(
            "ERROR: parsec_vdd package is incomplete: " + ", ".join(missing_parsec_vdd),
            file=sys.stderr,
        )
        sys.exit(1)
    os.makedirs(parsec_vdd_dst, exist_ok=True)
    for rel in parsec_vdd_required:
        copy_file(
            os.path.join(parsec_vdd_src, rel),
            os.path.join(parsec_vdd_dst, rel),
        )
    print("  + parsec_vdd/  (validated signed driver package)")

    # ------------------------------------------------------------------
    # 9. Web frontends (vite build output) → dist/<name>/
    # ------------------------------------------------------------------
    def collect_web_frontend(rel_src_parts, dst_name):
        src = os.path.join(source_dir, *rel_src_parts)
        dst = os.path.join(dist_dir, dst_name)
        if not os.path.isdir(src):
            print(f"ERROR: {dst_name} build output not found: {src}", file=sys.stderr)
            print(
                f"Run build_official.bat or: cd {'/'.join(rel_src_parts[:-1])} && npm run build",
                file=sys.stderr,
            )
            sys.exit(1)
        if not os.path.isfile(os.path.join(src, "index.html")):
            print(f"ERROR: {dst_name} dist is incomplete (missing index.html): {src}", file=sys.stderr)
            sys.exit(1)
        # Replace atomically so stale vite hashed assets cannot linger.
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        print(f"  + {dst_name}/  (from {src})")

    collect_web_frontend(("web", "px_web_client", "dist"), "web_client")
    collect_web_frontend(("web", "px_console", "dist"), "px_console")

    print(f"\nDone. Dist folder: {dist_dir}")


if __name__ == "__main__":
    main()
