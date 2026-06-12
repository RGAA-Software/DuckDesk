import argparse
import json
import os
import subprocess


def load_config():
    config_path = "make_setup_config.json"
    with open(config_path, "r", encoding="utf-8") as f:
        return json.load(f)


def run_7z(seven_zip_path, target_dir, output_7z):
    print("Running 7z compression...")

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

    print("Running NSIS to generate installer...")

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

    seven_zip_path = cfg["7z_path"]
    nsis_dir = cfg["nsis_dir_path"]

    # Python 文件当前目录
    current_dir = os.path.dirname(os.path.abspath(__file__))

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
