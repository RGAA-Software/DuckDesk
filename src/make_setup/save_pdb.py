
import os
import shutil

# gen_pack_name文件在 GammaRay\package 目录下
from gen_pack_name import gen_package_pdb_name

def copy_all_pdb(src_dir, dst_dir):
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)

    script_dir = os.path.abspath(os.path.dirname(__file__))  # 当前 py 文件所在目录

    for root, dirs, files in os.walk(src_dir, topdown=True):
        abs_root = os.path.abspath(root)

        # 排除脚本所在目录及其所有子目录
        if abs_root.startswith(script_dir):
            continue

        # 阻止 os.walk 进入脚本目录
        dirs[:] = [
            d for d in dirs
            if not os.path.abspath(os.path.join(root, d)).startswith(script_dir)
        ]

        for f in files:
            if f.lower().endswith(".pdb"):
                src_file = os.path.join(root, f)
                shutil.copy2(src_file, dst_dir)
                print(f"Copied: {src_file} → {dst_dir}")

if __name__ == "__main__":
    src_directory = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    print("src_directory:" + src_directory)

    # Python 文件当前目录
    current_dir = os.path.dirname(os.path.abspath(__file__))

    # 目标文件夹
    target_name = gen_package_pdb_name()
    target_dir = os.path.join(current_dir, target_name)

    copy_all_pdb(src_directory, target_dir)
