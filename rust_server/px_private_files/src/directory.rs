use crate::{FileError, Result};
use std::fs::File;
#[cfg(target_os = "linux")]
#[path = "directory_unix.rs"]
mod platform;
#[cfg(not(any(target_os = "linux", windows)))]
compile_error!("Private cache supports Windows and Linux only.");
#[cfg(windows)]
#[path = "directory_windows.rs"]
mod platform;
pub(crate) use platform::Directory;
#[derive(Clone, Copy)]
pub(crate) enum Access {
    Read,
    Create,
    Lock,
}
fn valid_name(name: &str) -> Result<()> {
    if name.is_empty()
        || name.len() > 64
        || name.contains("..")
        || !name.bytes().all(|byte_value| {
            byte_value.is_ascii_alphanumeric() || byte_value == b'.' || byte_value == b'-'
        })
    {
        return Err(FileError::InvalidInput);
    }
    Ok(())
}
fn check_file(file: &File) -> Result<()> {
    if !file.metadata()?.is_file() {
        return Err(FileError::Permission);
    }
    crate::private::check_permissions(file).map_err(|_| FileError::Permission)?;
    platform::check_file_links(file)?;
    Ok(())
}
