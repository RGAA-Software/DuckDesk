import argparse
import os
import shutil

from gen_pack_name import extract_project_version


def copy_all_pdb(src_dir, dst_dir):
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)

    for root, dirs, files in os.walk(src_dir, topdown=True):
        for f in files:
            if f.lower().endswith(".pdb"):
                src_file = os.path.join(root, f)
                shutil.copy2(src_file, dst_dir)
                print(f"Copied: {src_file} -> {dst_dir}")


def compute_output_dir(build_dir: str, current_dir: str) -> str:
    build_name = os.path.basename(os.path.normpath(build_dir))
    version_file = os.path.join(build_dir, "src", "gr_base", "version_config.h")
    version = extract_project_version(version_file)
    if not version:
        raise RuntimeError(f"Cannot extract PROJECT_VERSION from {version_file}")
    output_dir = os.path.join(current_dir, "..", "output", build_name, version)
    return os.path.abspath(output_dir)


def main():
    parser = argparse.ArgumentParser(description="Collect PDB files from build dir")
    parser.add_argument("--build-dir", required=True, help="CMake binary dir")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    current_dir = os.path.dirname(os.path.abspath(__file__))

    # 输出目录：output/<build_name>/<version>/
    output_dir = compute_output_dir(build_dir, current_dir)

    # 目标文件夹
    target_name = "GammaRay_pdb_" + extract_project_version(
        os.path.join(build_dir, "src", "gr_base", "version_config.h")
    )
    target_dir = os.path.join(output_dir, target_name)

    copy_all_pdb(build_dir, target_dir)
    print(f"PDB collection finished. Output: {target_dir}")


if __name__ == "__main__":
    main()
