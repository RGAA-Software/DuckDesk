use super::{check_file, valid_name, Access};
use crate::{FileError, Result};
use std::{
    fs::{File, OpenOptions},
    os::windows::{
        ffi::OsStrExt,
        fs::{MetadataExt, OpenOptionsExt},
        io::AsRawHandle,
    },
    path::{Component, Path, PathBuf, Prefix},
};
use windows_sys::Win32::Storage::FileSystem::{
    GetDriveTypeW, GetFileInformationByHandle, GetVolumeInformationByHandleW, MoveFileExW,
    BY_HANDLE_FILE_INFORMATION, MOVEFILE_WRITE_THROUGH,
};

pub(crate) struct Directory {
    file: File,
    path: PathBuf,
    _ancestors: Vec<File>,
}
fn open_directory(path: &Path) -> Result<File> {
    // Keep every ancestor open without delete sharing. Path components cannot be renamed/replaced while held.
    let file = OpenOptions::new()
        .read(true)
        .share_mode(3)
        .custom_flags(0x02200000)
        .open(path)?;
    let metadata = file.metadata()?;
    if !metadata.is_dir() || metadata.file_attributes() & 0x400 != 0 {
        return Err(FileError::Permission);
    }
    Ok(file)
}
impl Directory {
    pub(crate) fn open(path: &Path) -> Result<Self> {
        if !path.is_absolute() {
            return Err(FileError::InvalidInput);
        }
        let mut held = Vec::new();
        let mut current = PathBuf::new();
        for component in path.components() {
            match component {
                Component::Prefix(prefix) => {
                    if !matches!(prefix.kind(), Prefix::Disk(_) | Prefix::VerbatimDisk(_)) {
                        return Err(FileError::InvalidInput);
                    }
                    current.push(component.as_os_str());
                }
                Component::RootDir | Component::Normal(_) => {
                    current.push(component.as_os_str());
                    held.push(open_directory(&current)?);
                }
                _ => return Err(FileError::InvalidInput),
            }
        }
        let file = held.pop().ok_or(FileError::InvalidInput)?;
        let drive = current.ancestors().last().ok_or(FileError::InvalidInput)?;
        let drive: Vec<u16> = drive.as_os_str().encode_wide().chain(Some(0)).collect();
        if unsafe { GetDriveTypeW(drive.as_ptr()) } != 3 {
            return Err(FileError::InvalidInput);
        } // DRIVE_FIXED
        let mut filesystem = [0_u16; 32];
        if unsafe {
            GetVolumeInformationByHandleW(
                file.as_raw_handle(),
                std::ptr::null_mut(),
                0,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                filesystem.as_mut_ptr(),
                filesystem.len() as u32,
            )
        } == 0
        {
            return Err(std::io::Error::last_os_error().into());
        }
        if filesystem[..5] != [78, 84, 70, 83, 0] {
            return Err(FileError::InvalidInput);
        } // NTFS
        crate::private::check_permissions(&file).map_err(|_| FileError::Permission)?;
        Ok(Self {
            file,
            path: current,
            _ancestors: held,
        })
    }
    pub(crate) fn open_file(&self, name: &str, access: Access) -> Result<File> {
        valid_name(name)?;
        let mut options = OpenOptions::new();
        options.read(true).share_mode(3).custom_flags(0x00200000);
        match access {
            Access::Read => {}
            Access::Create => {
                options.write(true).create_new(true);
            }
            Access::Lock => {
                options.write(true).create(true);
            }
        }
        let file = options.open(self.path.join(name))?;
        check_file(&file)?;
        Ok(file)
    }
    pub(crate) fn is_empty(&self) -> Result<bool> {
        Ok(std::fs::read_dir(&self.path)?.next().transpose()?.is_none())
    }
    pub(crate) fn publish(&self, source: &str, target: &str) -> Result<()> {
        valid_name(source)?;
        valid_name(target)?;
        let source: Vec<u16> = self
            .path
            .join(source)
            .as_os_str()
            .encode_wide()
            .chain(Some(0))
            .collect();
        let target: Vec<u16> = self
            .path
            .join(target)
            .as_os_str()
            .encode_wide()
            .chain(Some(0))
            .collect();
        // No REPLACE_EXISTING: an old attempt can never overwrite a published object.
        if unsafe { MoveFileExW(source.as_ptr(), target.as_ptr(), MOVEFILE_WRITE_THROUGH) } == 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        Ok(())
    }
    pub(crate) fn remove(&self, name: &str) -> Result<bool> {
        valid_name(name)?;
        let file = match OpenOptions::new()
            .read(true)
            .share_mode(3)
            .custom_flags(0x00200000)
            .open(self.path.join(name))
        {
            Ok(file) => file,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(false),
            Err(error) => return Err(error.into()),
        };
        check_file(&file)?;
        drop(file);
        std::fs::remove_file(self.path.join(name))?;
        Ok(true)
    }
    pub(crate) fn sync(&self) -> Result<()> {
        // Files are FlushFileBuffers'd and publication uses WRITE_THROUGH. Startup must
        // still verify all referenced blobs: a PG backup cannot prove external media survived.
        let _ = &self.file;
        Ok(())
    }
}
pub(super) fn check_file_links(file: &File) -> Result<()> {
    let mut info: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
    if unsafe { GetFileInformationByHandle(file.as_raw_handle(), &mut info) } == 0 {
        return Err(std::io::Error::last_os_error().into());
    }
    if info.nNumberOfLinks != 1 || info.dwFileAttributes & 0x400 != 0 {
        return Err(FileError::Permission);
    }
    Ok(())
}
