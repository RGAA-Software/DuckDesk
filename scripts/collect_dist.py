#!/usr/bin/env python3
"""Collect build artifacts into a unified dist/ folder.

Copies directly from each target's native build directory.
No intermediate CMake copy steps required.
"""

import argparse
import os
import shutil
import sys

# Directories inside src/gr_deps/ that we keep as-is
KEEP_DIRS = {
    "certs", "resources", "translations", "www", "web", "package",
    "generic", "iconengines", "imageformats", "networkinformation",
    "platforms", "styles", "tls",
}

# Build-system dirs to skip when scanning
SKIP_DIRS = {"CMakeFiles", "deps", "tc_client_web"}

# File extensions to skip
SKIP_EXTS = {".pdb", ".ilk", ".lib", ".exp", ".obj", ".res", ".manifest", ".cmake"}

# Stale bundled FFmpeg DLLs that should not be copied now that FFmpeg is statically linked via vcpkg
SKIP_NAMES = {
    "avcodec-61.dll", "avdevice-61.dll", "avfilter-10.dll", "avformat-61.dll",
    "avutil-59.dll", "postproc-58.dll", "swresample-5.dll", "swscale-8.dll",
    # Disabled in config_premium.cmake (PLUGIN_NET_UDP_ENABLED=OFF); stale build
    # artifacts under src/gr_render/plugins/net_udp/ must not be repackaged.
    "plugin_net_udp.dll",
}

# Test executable prefix
TEST_PREFIX = "test_"

# Product executables we want to keep (basename match)
PRODUCT_EXES = {
    "GammaRay.exe",
    "GammaRayClientInner.exe",
    "GammaRayRender.exe",
    "GammaRayService.exe",
    "GammaRayServiceManager.exe",
    "GammaRayGuard.exe",
    "GammaRaySysInfo.exe",
    "GammaRayUserProxy.exe",
    "GammaRayCrashReporter.exe",
    "GammaRayUninstall.exe",
    "joystick.exe",
    "tc_graphics_util.exe",
    "tc_graphics_offsets.exe",
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
        shutil.copy2(src, dst)
        print(f"  + {os.path.relpath(dst, dist_dir)}")
    else:
        print(f"  - missing: {src}")


def main():
    parser = argparse.ArgumentParser(description="Collect build artifacts to dist/")
    parser.add_argument("--build-dir", required=True, help="CMake binary dir")
    args = parser.parse_args()

    global dist_dir
    build_dir = os.path.abspath(args.build_dir)
    source_dir = os.path.abspath(os.path.join(build_dir, ".."))
    dist_dir = os.path.join(build_dir, "dist")

    if os.path.isdir(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir, exist_ok=True)

    # ------------------------------------------------------------------
    # 1. Base: src/gr_deps/ (main exes, Qt DLLs, resources, etc.)
    # ------------------------------------------------------------------
    gamma_ray_dir = os.path.join(build_dir, "src", "gr_deps")
    if os.path.isdir(gamma_ray_dir):
        for entry in os.listdir(gamma_ray_dir):
            src_path = os.path.join(gamma_ray_dir, entry)
            dst_path = os.path.join(dist_dir, entry)
            if os.path.isdir(src_path):
                if entry in KEEP_DIRS:
                    copy_tree(src_path, dst_path)
                elif entry.startswith("tc_"):
                    # skip submodule build output directories
                    pass
                elif entry not in SKIP_DIRS:
                    copy_tree(src_path, dst_path)
            else:
                if should_copy_file(entry):
                    shutil.copy2(src_path, dst_path)

    # ------------------------------------------------------------------
    # 2. Supplementary executables / DLLs from native build dirs
    # ------------------------------------------------------------------
    supplements = [
        ("src/gr_client/GammaRayClientInner.exe", "GammaRayClientInner.exe"),
        # SDL2/gflags/fftw3 are now statically linked via x64-windows-static-release
        ("libplacebo-349.dll", "libplacebo-349.dll"),
        ("src/gr_render/GammaRayRender.exe", "GammaRayRender.exe"),
        ("src/gr_render/app/tc_global_id_generator.dll", "tc_global_id_generator.dll"),
        ("src/gr_deps/tc_webrtc_client/tc_rtc_client.dll", "gr_client/tc_rtc_client.dll"),
    ]
    for rel_src, rel_dst in supplements:
        copy_file(rel_src if os.path.isabs(rel_src) else os.path.join(build_dir, rel_src), os.path.join(dist_dir, rel_dst))

    # ------------------------------------------------------------------
    # 3. Render plugins  →  dist/gr_plugins/
    # ------------------------------------------------------------------
    gr_plugins_build_dir = os.path.join(build_dir, "src", "gr_render", "plugins")
    gr_plugins_src_dir = os.path.join(source_dir, "src", "gr_render", "plugins")
    gr_plugins_dst = os.path.join(dist_dir, "gr_plugins")
    if os.path.isdir(gr_plugins_build_dir):
        os.makedirs(gr_plugins_dst, exist_ok=True)
        for plugin_dir in os.listdir(gr_plugins_build_dir):
            plugin_build_dir = os.path.join(gr_plugins_build_dir, plugin_dir)
            plugin_src_dir = os.path.join(gr_plugins_src_dir, plugin_dir)
            if not os.path.isdir(plugin_build_dir):
                continue
            for f in os.listdir(plugin_build_dir):
                if f.startswith("plugin_") and f.endswith(".dll") and should_copy_file(f):
                    copy_file(os.path.join(plugin_build_dir, f), os.path.join(gr_plugins_dst, f))
            if os.path.isdir(plugin_src_dir):
                for f in os.listdir(plugin_src_dir):
                    if f.startswith("plugin_") and f.endswith(".toml"):
                        copy_file(os.path.join(plugin_src_dir, f), os.path.join(gr_plugins_dst, f))

    # ------------------------------------------------------------------
    # 4. Client plugins  →  dist/gr_plugins_client/
    # ------------------------------------------------------------------
    client_plugin_dirs = ["clipboard", "file_transfer_client", "media_record", "multi_screens"]
    gr_plugins_client_dst = os.path.join(dist_dir, "gr_plugins_client")
    for plugin_dir in client_plugin_dirs:
        plugin_build_dir = os.path.join(build_dir, "src", "gr_client", "plugins", plugin_dir)
        plugin_src_dir = os.path.join(source_dir, "src", "gr_client", "plugins", plugin_dir)
        if not os.path.isdir(plugin_build_dir):
            continue
        os.makedirs(gr_plugins_client_dst, exist_ok=True)
        for f in os.listdir(plugin_build_dir):
            if f.startswith("plugin_") and f.endswith(".dll") and should_copy_file(f):
                copy_file(os.path.join(plugin_build_dir, f), os.path.join(gr_plugins_client_dst, f))
        if os.path.isdir(plugin_src_dir):
            for f in os.listdir(plugin_src_dir):
                if f.startswith("plugin_") and f.endswith(".toml"):
                    copy_file(os.path.join(plugin_src_dir, f), os.path.join(gr_plugins_client_dst, f))

    # ------------------------------------------------------------------
    # 5. Skins  →  dist/gr_skins/
    # ------------------------------------------------------------------
    gr_skins_dst = os.path.join(dist_dir, "gr_skins")
    os.makedirs(gr_skins_dst, exist_ok=True)

    # skin_open_source / skin_official DLLs
    # Because CMAKE_RUNTIME_OUTPUT_DIRECTORY is redirected to GR_PROJECT_BINARY_PATH
    # (which points to src/gr_deps), the skin DLLs are built there, not under
    # src/gr_panel/src/skin/official.
    skins_src = os.path.join(build_dir, "src", "gr_deps")
    if os.path.isdir(skins_src):
        for f in os.listdir(skins_src):
            if f.startswith("skin_") and f.endswith(".dll"):
                copy_file(os.path.join(skins_src, f), os.path.join(gr_skins_dst, f))

    # skin config (from source tree)
    skin_config_src = os.path.join(build_dir, "..", "src", "gr_panel", "src", "skin", "skin_config.toml")
    if os.path.isfile(skin_config_src):
        copy_file(skin_config_src, os.path.join(gr_skins_dst, "skin_config.toml"))

    # ------------------------------------------------------------------
    # 6. Hook capture
    # ------------------------------------------------------------------
    hook_capture_files = [
        ("src/gr_render/hook_capture/win/hk_obs/tc_graphics.dll", "tc_graphics.dll"),
        ("src/gr_render/hook_capture/win/hk_obs/injector/tc_graphics_util.exe", "tc_graphics_util.exe"),
        ("src/gr_render/hook_capture/win/hk_obs/offsets/tc_graphics_offsets.exe", "tc_graphics_offsets.exe"),
    ]
    for rel_src, rel_dst in hook_capture_files:
        copy_file(os.path.join(build_dir, rel_src), os.path.join(dist_dir, rel_dst))

    # ------------------------------------------------------------------
    # 7. Anti-hooking
    # ------------------------------------------------------------------
    copy_file(
        os.path.join(build_dir, "src", "gr_client", "anti_hooking", "tc_protection.dll"),
        os.path.join(dist_dir, "tc_protection.dll"),
    )

    # ------------------------------------------------------------------
    # 8. Joystick (source tree)
    # ------------------------------------------------------------------
    joystick_src = os.path.join(build_dir, "..", "src", "gr_deps", "tc_controller", "vigem", "driver", "joystick.exe")
    if os.path.isfile(joystick_src):
        copy_file(joystick_src, os.path.join(dist_dir, "joystick.exe"))

    print(f"\nDone. Dist folder: {dist_dir}")


if __name__ == "__main__":
    main()
