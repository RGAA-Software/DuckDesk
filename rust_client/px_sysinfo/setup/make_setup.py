import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path


PRODUCT_NAME = "PxSysMonitor Suite"
INSTALLER_BASENAME = "PxSysMonitorSuite"
MONITOR_EXE = "px_sys_monitor.exe"
HOST_EXE = "px_sys_monitor_host.exe"


def load_config(config_path: Path) -> dict:
    if config_path.is_file():
        return json.loads(config_path.read_text(encoding="utf-8"))
    return {}


def find_existing_path(candidates: list[Path], description: str) -> Path:
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Cannot find {description}. Checked: {candidates}")


def find_7z(config: dict, script_dir: Path) -> Path:
    configured = config.get("7z_path")
    candidates: list[Path] = []
    if configured:
        candidates.append((script_dir / configured).resolve())
    candidates.extend(
        [
            (script_dir / ".." / ".." / ".." / "tools" / "7z" / "7za.exe").resolve(),
            Path(r"C:\Program Files\7-Zip\7z.exe"),
            Path(r"C:\Program Files (x86)\7-Zip\7z.exe"),
        ]
    )
    return find_existing_path(candidates, "7-Zip executable")


def find_makensis(config: dict, script_dir: Path) -> Path:
    configured = config.get("nsis_path")
    candidates: list[Path] = []
    if configured:
        candidates.append((script_dir / configured).resolve())
    candidates.extend(
        [
            (script_dir / ".." / ".." / ".." / "tools" / "nsis" / "makensis.exe").resolve(),
            Path(r"C:\Program Files (x86)\NSIS\makensis.exe"),
            Path(r"C:\Program Files\NSIS\makensis.exe"),
        ]
    )
    return find_existing_path(candidates, "NSIS makensis.exe")


def read_workspace_version(workspace_dir: Path) -> str:
    cargo_toml = workspace_dir / "Cargo.toml"
    content = cargo_toml.read_text(encoding="utf-8")
    match = re.search(r"(?ms)^\[workspace\.package\].*?^version\s*=\s*\"([^\"]+)\"", content)
    if not match:
        raise RuntimeError(f"Cannot read workspace version from {cargo_toml}")
    return match.group(1)


def run_checked(cmd: list[str], cwd: Path) -> None:
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=str(cwd), check=True)


def build_release_binaries(workspace_dir: Path) -> None:
    run_checked(
        ["cargo", "build", "-p", "px_sysinfo", "--release", "--bin", "px_sys_monitor"],
        workspace_dir,
    )
    run_checked(
        ["cargo", "build", "-p", "px_sysinfo", "--release", "--bin", "px_sys_monitor_host"],
        workspace_dir,
    )


def stage_payload(workspace_dir: Path, stage_dir: Path) -> None:
    release_dir = workspace_dir / "target" / "release"
    monitor_path = release_dir / MONITOR_EXE
    host_path = release_dir / HOST_EXE
    if not monitor_path.is_file():
        raise RuntimeError(f"Missing release binary: {monitor_path}")
    if not host_path.is_file():
        raise RuntimeError(f"Missing release binary: {host_path}")

    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(monitor_path, stage_dir / MONITOR_EXE)
    shutil.copy2(host_path, stage_dir / HOST_EXE)


def build_archive(seven_zip: Path, stage_dir: Path, archive_path: Path) -> None:
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    if archive_path.exists():
        archive_path.unlink()
    run_checked(
        [str(seven_zip), "a", "-t7z", str(archive_path), str(stage_dir / "*")],
        stage_dir,
    )


def build_nsis(
    makensis: Path,
    script_dir: Path,
    output_dir: Path,
    version: str,
) -> None:
    nsi_script = script_dir / "make_setup.nsi"
    run_checked(
        [
            str(makensis),
            f"/DOUTPUT_DIR={output_dir}",
            f"/DPRODUCT_VERSION={version}",
            f"/DINSTALLER_BASENAME={INSTALLER_BASENAME}",
            str(nsi_script),
        ],
        script_dir,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build px_sys_monitor installer")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip cargo release build and only package existing binaries",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    crate_dir = script_dir.parent
    workspace_dir = crate_dir.parent

    config = load_config(script_dir / "make_setup_config.json")
    seven_zip = find_7z(config, script_dir)
    makensis = find_makensis(config, script_dir)
    version = read_workspace_version(workspace_dir)

    if not args.skip_build:
        build_release_binaries(workspace_dir)

    build_root = crate_dir / "build"
    stage_dir = build_root / "setup_stage"
    output_dir = build_root / "setup_output" / version
    archive_path = output_dir / "app" / "app.7z"

    stage_payload(workspace_dir, stage_dir)
    build_archive(seven_zip, stage_dir, archive_path)
    build_nsis(makensis, script_dir, output_dir, version)

    print(f"Installer ready: {output_dir / f'{INSTALLER_BASENAME}_{version}_Setup.exe'}")


if __name__ == "__main__":
    main()
