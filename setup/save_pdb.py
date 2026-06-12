import argparse
import os
import shutil

from gen_pack_name import gen_package_pdb_name


def copy_all_pdb(src_dir, dst_dir):
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)

    for root, dirs, files in os.walk(src_dir, topdown=True):
        for f in files:
            if f.lower().endswith(".pdb"):
                src_file = os.path.join(root, f)
                shutil.copy2(src_file, dst_dir)
                print(f"Copied: {src_file} -> {dst_dir}")


def main():
    parser = argparse.ArgumentParser(description="Collect PDB files from build dir")
    parser.add_argument("--build-dir", required=True, help="CMake binary dir")
    args = parser.parse_args()

    build_dir = os.path.abspath(args.build_dir)

    # Python 文件当前目录
    current_dir = os.path.dirname(os.path.abspath(__file__))

    # 目标文件夹
    target_name = gen_package_pdb_name(build_dir)
    target_dir = os.path.join(current_dir, target_name)

    copy_all_pdb(build_dir, target_dir)


if __name__ == "__main__":
    main()
