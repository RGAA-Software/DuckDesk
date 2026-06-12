use std::{fs, io};
use std::path::Path;

pub fn create_dir_if_not_exists(path: &str) -> io::Result<()> {
    match fs::create_dir(path) {
        Ok(_) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(()),
        Err(e) => Err(e),
    }
}

pub fn delete_dir_if_exists(path: &str) -> io::Result<()> {
    fs::remove_dir_all(path)
}


/// 复制目录及其所有内容，如果目标目录存在则强制覆盖
///
/// # 参数
/// - `src`: 源目录路径
/// - `dst`: 目标目录路径
///
/// # 返回值
/// - `Ok(())`: 成功复制
/// - `Err(io::Error)`: 复制过程中出现错误
///
/// # 示例
/// ```
/// copy_dir_all("/path/to/source", "/path/to/destination").unwrap();
/// ```
pub fn copy_dir_all(src: impl AsRef<Path>, dst: impl AsRef<Path>) -> io::Result<()> {
    let src = src.as_ref();
    let dst = dst.as_ref();

    // 确保源目录存在
    if !src.is_dir() {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("Source directory does not exist: {:?}", src),
        ));
    }

    // 如果目标目录存在，先删除它（强制覆盖）
    if dst.exists() {
        fs::remove_dir_all(dst)?;
    }

    // 创建目标目录
    fs::create_dir_all(dst)?;

    // 遍历源目录并复制所有内容
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());

        // 根据文件类型进行不同的复制操作
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            // 递归复制子目录
            copy_dir_all(&src_path, &dst_path)?;
        } else if file_type.is_symlink() {
            // 处理符号链接
            let link_target = fs::read_link(&src_path)?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::symlink;
                symlink(link_target, &dst_path)?;
            }
            #[cfg(windows)]
            {
                use std::os::windows::fs::symlink_file;
                // Windows需要区分文件和目录的符号链接
                if link_target.is_dir() {
                    std::os::windows::fs::symlink_dir(link_target, &dst_path)?;
                } else {
                    symlink_file(link_target, &dst_path)?;
                }
            }
            #[cfg(not(any(unix, windows)))]
            {
                // 对于不支持符号链接的平台，复制文件内容
                if link_target.is_file() {
                    fs::copy(&link_target, &dst_path)?;
                }
            }
        } else {
            // 复制普通文件
            fs::copy(&src_path, &dst_path)?;
        }
    }

    Ok(())
}