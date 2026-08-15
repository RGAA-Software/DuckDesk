import os
import re


def extract_project_version(file_path):
    with open(file_path, 'r') as file:
        content = file.read()

    # 匹配 #define PROJECT_VERSION "x.x.x"
    match = re.search(r'#define\s+PROJECT_VERSION\s+"([0-9.]+)"', content)
    if match:
        return match.group(1)
    return None


def gen_package_name(build_dir: str) -> str:
    version_file = os.path.join(build_dir, "src", "px_base", "version_config.h")
    version = extract_project_version(version_file)
    if not version:
        raise RuntimeError(f"Cannot extract PROJECT_VERSION from {version_file}")
    target_name = "GammaRay_" + version
    return target_name


def gen_package_pdb_name(build_dir: str) -> str:
    version_file = os.path.join(build_dir, "src", "px_base", "version_config.h")
    version = extract_project_version(version_file)
    if not version:
        raise RuntimeError(f"Cannot extract PROJECT_VERSION from {version_file}")
    target_name = "GammaRay_pdb_" + version
    return target_name
