use super::{check_file, valid_name, Access};
use crate::{FileError, Result};
use std::{
    ffi::CString,
    fs::File,
    os::fd::{AsRawFd, FromRawFd},
    os::unix::fs::MetadataExt,
    path::{Component, Path},
};

pub(crate) struct Directory {
    file: File,
}
impl Directory {
    pub(crate) fn open(path: &Path) -> Result<Self> {
        if !path.is_absolute() {
            return Err(FileError::InvalidInput);
        }
        let mut file = File::open("/")?;
        for component in path.components() {
            match component {
                Component::RootDir => {}
                Component::Normal(name) => {
                    use std::os::unix::ffi::OsStrExt;
                    let name =
                        CString::new(name.as_bytes()).map_err(|_| FileError::InvalidInput)?;
                    // Borrowed directory FD is stable across rename; reject symlinks at every component.
                    let fd = unsafe {
                        libc::openat(
                            file.as_raw_fd(),
                            name.as_ptr(),
                            libc::O_RDONLY
                                | libc::O_CLOEXEC
                                | libc::O_NONBLOCK
                                | libc::O_NOFOLLOW
                                | libc::O_DIRECTORY,
                        )
                    };
                    if fd < 0 {
                        return Err(std::io::Error::last_os_error().into());
                    }
                    file = unsafe { File::from_raw_fd(fd) };
                }
                _ => return Err(FileError::InvalidInput),
            }
        }
        crate::private::check_permissions(&file).map_err(|_| FileError::Permission)?;
        let metadata = file.metadata()?;
        if !metadata.is_dir()
            || (metadata.uid() != unsafe { libc::geteuid() } && metadata.uid() != 0)
        {
            return Err(FileError::Permission);
        }
        let mut filesystem: libc::statfs = unsafe { std::mem::zeroed() };
        if unsafe { libc::fstatfs(file.as_raw_fd(), &mut filesystem) } != 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        // Linux ext-family local filesystem only in this baseline (ext4 is the acceptance target).
        // Network/FUSE/overlay/tmpfs roots need their own durability/locking validation first.
        if filesystem.f_type != libc::EXT4_SUPER_MAGIC {
            return Err(FileError::InvalidInput);
        }
        Ok(Self { file })
    }
    pub(crate) fn open_file(&self, name: &str, access: Access) -> Result<File> {
        valid_name(name)?;
        let name = CString::new(name).map_err(|_| FileError::InvalidInput)?;
        let flags = libc::O_CLOEXEC
            | libc::O_NONBLOCK
            | libc::O_NOFOLLOW
            | match access {
                Access::Read => libc::O_RDONLY,
                Access::Create => libc::O_RDWR | libc::O_CREAT | libc::O_EXCL,
                Access::Lock => libc::O_RDWR | libc::O_CREAT,
            };
        let fd = unsafe { libc::openat(self.file.as_raw_fd(), name.as_ptr(), flags, 0o600) };
        if fd < 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        let file = unsafe { File::from_raw_fd(fd) };
        check_file(&file)?;
        Ok(file)
    }
    pub(crate) fn is_empty(&self) -> Result<bool> {
        // Supported service Unix target is Linux. Enumerate the anchored directory, not a replaceable pathname.
        let path = format!("/proc/self/fd/{}", self.file.as_raw_fd());
        Ok(std::fs::read_dir(path)?.next().transpose()?.is_none())
    }
    pub(crate) fn publish(&self, source: &str, target: &str) -> Result<()> {
        valid_name(source)?;
        valid_name(target)?;
        let source = CString::new(source).map_err(|_| FileError::InvalidInput)?;
        let target = CString::new(target).map_err(|_| FileError::InvalidInput)?;
        // Linux renameat2 is atomic and refuses replacement. Unsupported filesystems fail
        // closed; never fall back to check-then-rename or link/unlink publication.
        if unsafe {
            libc::renameat2(
                self.file.as_raw_fd(),
                source.as_ptr(),
                self.file.as_raw_fd(),
                target.as_ptr(),
                libc::RENAME_NOREPLACE,
            )
        } != 0
        {
            return Err(std::io::Error::last_os_error().into());
        }
        self.sync()
    }
    pub(crate) fn remove(&self, name: &str) -> Result<bool> {
        valid_name(name)?;
        let name_c = CString::new(name).map_err(|_| FileError::InvalidInput)?;
        let fd = unsafe {
            libc::openat(
                self.file.as_raw_fd(),
                name_c.as_ptr(),
                libc::O_RDONLY | libc::O_CLOEXEC | libc::O_NONBLOCK | libc::O_NOFOLLOW,
            )
        };
        if fd < 0 {
            let error = std::io::Error::last_os_error();
            return if error.kind() == std::io::ErrorKind::NotFound {
                Ok(false)
            } else {
                Err(error.into())
            };
        }
        let file = unsafe { File::from_raw_fd(fd) };
        check_file(&file)?;
        if unsafe { libc::unlinkat(self.file.as_raw_fd(), name_c.as_ptr(), 0) } != 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        self.sync()?;
        Ok(true)
    }
    pub(crate) fn sync(&self) -> Result<()> {
        self.file.sync_all()?;
        Ok(())
    }
}
pub(super) fn check_file_links(file: &File) -> Result<()> {
    let metadata = file.metadata()?;
    if metadata.nlink() != 1
        || (metadata.uid() != unsafe { libc::geteuid() } && metadata.uid() != 0)
    {
        return Err(FileError::Permission);
    }
    Ok(())
}
