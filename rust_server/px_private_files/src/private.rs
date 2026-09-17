//! Private material is read through the same handle whose type and permissions were checked.
use std::{
    fs::{File, OpenOptions},
    io::Read,
    path::Path,
};
use zeroize::Zeroizing;

/// Explicit provisioning only. Restrict the parent first; never overwrite an existing key.
pub fn create_private(path: &Path, bytes: &[u8]) -> Result<(), &'static str> {
    use std::io::Write;
    if bytes.is_empty() || bytes.len() > 4096 {
        return Err("invalid private material size");
    }
    let parent = path
        .parent()
        .filter(|value| !value.as_os_str().is_empty())
        .ok_or("private material requires an explicit parent directory")?;
    let mut directory_options = OpenOptions::new();
    directory_options.read(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        directory_options.share_mode(1).custom_flags(0x02200000);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        directory_options.custom_flags(libc::O_NOFOLLOW | libc::O_DIRECTORY);
    }
    let directory = directory_options
        .open(parent)
        .map_err(|_| "private directory unavailable")?;
    let metadata = directory
        .metadata()
        .map_err(|_| "private directory unavailable")?;
    if !metadata.is_dir() {
        return Err("private parent must be a directory");
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if metadata.file_attributes() & 0x400 != 0 {
            return Err("private parent reparse point refused");
        }
    }
    check_permissions(&directory)?;
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(0);
    }
    let mut file = options
        .open(path)
        .map_err(|_| "private file creation refused (must not exist)")?;
    file.write_all(bytes)
        .map_err(|_| "private material write failed; incomplete file must be reviewed")?;
    file.sync_all()
        .map_err(|_| "private material sync failed; file must be reviewed")?;
    drop(file);
    if read_private(path)?.as_slice() != bytes {
        return Err("private material verification failed");
    }
    Ok(())
}

pub fn read_private(path: &Path) -> Result<Zeroizing<Vec<u8>>, &'static str> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(1).custom_flags(0x00200000); // No write/delete sharing; open reparse point itself.
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let file = options
        .open(path)
        .map_err(|_| "private material unavailable")?;
    let metadata = file
        .metadata()
        .map_err(|_| "private material unavailable")?;
    if !metadata.is_file() || metadata.len() == 0 || metadata.len() > 4096 {
        return Err("invalid private material file");
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if metadata.file_attributes() & 0x400 != 0 {
            return Err("private material reparse point refused");
        }
    }
    check_permissions(&file)?;
    let mut bytes = Zeroizing::new(Vec::new());
    file.take(4097)
        .read_to_end(&mut bytes)
        .map_err(|_| "private material read failed")?;
    if bytes.len() > 4096 {
        return Err("private material size changed");
    }
    Ok(bytes)
}

#[cfg(unix)]
pub(crate) fn check_permissions(file: &File) -> Result<(), &'static str> {
    use std::os::unix::fs::PermissionsExt;
    if file
        .metadata()
        .map_err(|_| "private material metadata unavailable")?
        .permissions()
        .mode()
        & 0o077
        != 0
    {
        return Err("private material must not be group/world accessible");
    }
    Ok(())
}

#[cfg(windows)]
pub(crate) fn check_permissions(file: &File) -> Result<(), &'static str> {
    use std::{
        ffi::c_void,
        os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle},
    };
    use windows_sys::Win32::{
        Foundation::LocalFree,
        Security::{
            Authorization::{GetSecurityInfo, SE_FILE_OBJECT},
            *,
        },
        System::Threading::{GetCurrentProcess, OpenProcessToken},
    };
    struct Descriptor(PSECURITY_DESCRIPTOR);
    impl Drop for Descriptor {
        fn drop(&mut self) {
            unsafe {
                LocalFree(self.0);
            }
        }
    }
    let mut owner = std::ptr::null_mut();
    let mut dacl = std::ptr::null_mut();
    let mut descriptor = Descriptor(std::ptr::null_mut());
    // GetSecurityInfo owns the returned self-relative descriptor until LocalFree.
    let result = unsafe {
        GetSecurityInfo(
            file.as_raw_handle(),
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &mut owner,
            std::ptr::null_mut(),
            &mut dacl,
            std::ptr::null_mut(),
            &mut descriptor.0,
        )
    };
    if result != 0 || dacl.is_null() || owner.is_null() {
        return Err("private material ACL unavailable or unrestricted");
    }
    let mut raw = std::ptr::null_mut();
    if unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut raw) } == 0 {
        return Err("process identity unavailable");
    }
    let token = unsafe { OwnedHandle::from_raw_handle(raw) };
    let mut size = 0;
    unsafe {
        GetTokenInformation(
            token.as_raw_handle(),
            TokenUser,
            std::ptr::null_mut(),
            0,
            &mut size,
        );
    }
    if size == 0 || size > 65536 {
        return Err("invalid process identity");
    }
    // u64 storage provides alignment for TOKEN_USER; the SID remains borrowed from this buffer.
    let mut buffer = vec![0_u64; (size as usize).div_ceil(8)];
    if unsafe {
        GetTokenInformation(
            token.as_raw_handle(),
            TokenUser,
            buffer.as_mut_ptr().cast(),
            size,
            &mut size,
        )
    } == 0
    {
        return Err("process identity unavailable");
    }
    let current = unsafe { (*(buffer.as_ptr().cast::<TOKEN_USER>())).User.Sid };
    let trusted = |sid| unsafe {
        EqualSid(sid, current) != 0
            || IsWellKnownSid(sid, WinLocalSystemSid) != 0
            || IsWellKnownSid(sid, WinBuiltinAdministratorsSid) != 0
    };
    if !trusted(owner) {
        return Err("private material owner is not trusted");
    }
    for index in 0..unsafe { (*dacl).AceCount } {
        let mut ace: *mut c_void = std::ptr::null_mut();
        if unsafe { GetAce(dacl, u32::from(index), &mut ace) } == 0 || ace.is_null() {
            return Err("private material ACL invalid");
        }
        let header = unsafe { &*ace.cast::<ACE_HEADER>() };
        // Reject unfamiliar/object/callback allow ACEs, instead of accidentally allowing an unexamined grant.
        match header.AceType {
            0 => {
                let allowed = unsafe { &*ace.cast::<ACCESS_ALLOWED_ACE>() };
                let sid = std::ptr::addr_of!(allowed.SidStart)
                    .cast_mut()
                    .cast::<c_void>();
                if !trusted(sid) {
                    return Err("private material grants another principal access");
                }
            }
            1 => {} // A deny ACE cannot broaden access.
            _ => return Err("private material has an unsupported ACL entry"),
        }
    }
    Ok(())
}
