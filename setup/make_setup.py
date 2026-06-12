import argparse
import json
import os
import subprocess


def load_config():
    config_path = "make_setup_config.json"
    if os.path.isfile(config_path):
        with open(config_path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def find_7z(configured_path: str | None, current_dir: str) -> str:
    candidates = []
    if configured_path:
        candidates.append(configured_path)
    candidates.extend([
        os.path.join(current_dir, "..", "tools", "7z", "7za.exe"),
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
        r"D:\company\software\7-Zip\7z.exe",
        r"D:\software\7-Zip\7-Zip\7z.exe",
    ])
    for path in candidates:
        if os.path.isfile(path):
            return path
    raise RuntimeError(
        "Cannot find 7z.exe. Please install 7-Zip or update make_setup_config.json."
    )


def find_nsis(configured_dir: str | None, current_dir: str) -> str:
    candidates = []
    if configured_dir:
        candidates.append(os.path.join(configured_dir, "makensis.exe"))
    candidates.extend([
        os.path.join(current_dir, "..", "tools", "nsis", "makensis.exe"),
        r"C:\Program Files (x86)\NSIS\makensis.exe",
        r"C:\Program Files\NSIS\makensis.exe",
        r"D:\company\software\NSIS\makensis.exe",
        r"D:\software\newNSIS3.06.1\newNSIS3.06.1\makensis.exe",
    ])
    for path in candidates:
        if os.path.isfile(path):
            return os.path.dirname(path)
    raise RuntimeError(
        "Cannot find NSIS makensis.exe. Please install NSIS or update make_setup_config.json."
    )


def run_7z(seven_zip_path, target_dir, output_7z):
    print(f"Running 7z compression: {seven_zip_path}")

    os.makedirs(os.path.dirname(output_7z), exist_ok=True)

    cmd = [
        seven_zip_path,
        "a",                 # add
        "-t7z",              # format
        output_7z,
        f"{target_dir}/*"    # files to compress
    ]

    subprocess.run(cmd, check=True)
    print("7z compression completed.")


def run_nsis(nsis_dir, nsi_script_path, working_dir):
    makensis_exe = os.path.join(nsis_dir, "makensis.exe")

    print(f"Running NSIS to generate installer: {makensis_exe}")

    cmd = [
        makensis_exe,
        nsi_script_path
    ]

    subprocess.run(cmd, check=True, cwd=working_dir)
    print("NSIS build completed.")


def main():
    parser = argparse.ArgumentParser(description="Package dist into installer")
    parser.add_argument("--build-dir", required=True, help="CMake binary dir containing dist/")
    args = parser.parse_args()

    cfg = load_config()

    current_dir = os.path.dirname(os.path.abspath(__file__))

    seven_zip_path = find_7z(cfg.get("7z_path"), current_dir)
    nsis_dir = find_nsis(cfg.get("nsis_dir_path"), current_dir)

    build_dir = os.path.abspath(args.build_dir)

    # 目标压缩文件夹：直接使用编译好的 dist/
    target_dir = os.path.join(build_dir, "dist")
    if not os.path.isdir(target_dir):
        raise RuntimeError(f"dist folder not found: {target_dir}")

    # 输出 app.7z
    output_7z = os.path.join(current_dir, "app", "app.7z")

    # NSIS 脚本路径
    nsi_script_path = os.path.join(current_dir, "make_setup.nsi")

    # NSIS 的工作目录
    nsi_workdir = current_dir

    # 调用 7z 压缩
    run_7z(seven_zip_path, target_dir, output_7z)

    # 调用 NSIS 生成安装包
    run_nsis(nsis_dir, nsi_script_path, nsi_workdir)

    print("All tasks finished successfully.")


if __name__ == "__main__":
    main()
