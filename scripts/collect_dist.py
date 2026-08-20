#!/usr/bin/env python3
"""Collect build artifacts into a unified dist/ folder.

Copies directly from each target's native build directory.
No intermediate CMake copy steps required.
"""

import argparse
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

    # ------------------------------------------------------------------
    # 2. Supplementary executables / DLLs from native build dirs
    # ------------------------------------------------------------------
    supplements = [
        ("src/px_client/px_client.exe", "px_client.exe"),
        # SDL2/gflags/fftw3 are now statically linked via x64-windows-static-release
        ("libplacebo-349.dll", "libplacebo-349.dll"),
        ("src/px_render/px_render.exe", "px_render.exe"),
        ("src/px_deps/px_webrtc_client/px_rtc_client.dll", "px_client_rtc.dll"),
    ]
    for rel_src, rel_dst in supplements:
        copy_file(rel_src if os.path.isabs(rel_src) else os.path.join(build_dir, rel_src), os.path.join(dist_dir, rel_dst))

    # ------------------------------------------------------------------
    # 3. Render plugins  →  dist/deps/rd_plugins/
    # ------------------------------------------------------------------
    px_plugins_build_dir = os.path.join(build_dir, "src", "px_render", "plugins")
    px_plugins_dst = os.path.join(dist_dir, "deps", "rd_plugins")
    if os.path.isdir(px_plugins_build_dir):
        os.makedirs(px_plugins_dst, exist_ok=True)
        for plugin_dir in os.listdir(px_plugins_build_dir):
            plugin_build_dir = os.path.join(px_plugins_build_dir, plugin_dir)
            if not os.path.isdir(plugin_build_dir):
                continue
            for f in os.listdir(plugin_build_dir):
                if f.endswith(".dll") and should_copy_file(f):
                    copy_file(os.path.join(plugin_build_dir, f), os.path.join(px_plugins_dst, f))

    # ------------------------------------------------------------------
    # 4. Client plugins  →  dist/deps/ct_plugins/
    # ------------------------------------------------------------------
    client_plugin_dirs = ["clipboard", "ft", "media_record", "multi_screens"]
    px_plugins_client_dst = os.path.join(dist_dir, "deps", "ct_plugins")
    for plugin_dir in client_plugin_dirs:
        plugin_build_dir = os.path.join(build_dir, "src", "px_client", "plugins", plugin_dir)
        if not os.path.isdir(plugin_build_dir):
            continue
        os.makedirs(px_plugins_client_dst, exist_ok=True)
        for f in os.listdir(plugin_build_dir):
            if f.endswith(".dll") and should_copy_file(f):
                copy_file(os.path.join(plugin_build_dir, f), os.path.join(px_plugins_client_dst, f))

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
    collect_web_frontend(("web", "px_cms", "dist"), "px_cms")

    print(f"\nDone. Dist folder: {dist_dir}")


if __name__ == "__main__":
    main()
