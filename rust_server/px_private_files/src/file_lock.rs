use crate::{FileError, Result};
use std::fs::File;
pub(crate) struct FileLock {
    pub(crate) file: File,
}
impl FileLock {
    pub(crate) fn exclusive(file: File) -> Result<Self> {
        fs2::FileExt::try_lock_exclusive(&file).map_err(|_| FileError::Busy)?;
        Ok(Self { file })
    }
    pub(crate) fn shared(file: File) -> Result<Self> {
        fs2::FileExt::try_lock_shared(&file).map_err(|_| FileError::Busy)?;
        Ok(Self { file })
    }
}
impl Drop for FileLock {
    fn drop(&mut self) {
        let _ = fs2::FileExt::unlock(&self.file);
    }
}
