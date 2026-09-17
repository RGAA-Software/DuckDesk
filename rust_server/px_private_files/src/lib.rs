//! Private local files and immutable cache blobs; no database, network or global state.
//! Blocking file IO belongs on the application's bounded blocking executor.
mod cache;
mod directory;
mod file_lock;
pub mod private;
pub use cache::{
    BlobGuard, BlobReader, BlobWriter, CacheRoot, ContentIdentity, DeletedBlob, PublishedBlob,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum FileError {
    #[error("invalid private-file input")]
    InvalidInput,
    #[error("private file unavailable")]
    Unavailable,
    #[error("private file permission or type rejected")]
    Permission,
    #[error("private file already exists")]
    Exists,
    #[error("private file is busy")]
    Busy,
    #[error("private file requires reconciliation")]
    Corrupt,
}
type Result<T> = std::result::Result<T, FileError>;
impl From<std::io::Error> for FileError {
    fn from(error: std::io::Error) -> Self {
        match error.kind() {
            std::io::ErrorKind::AlreadyExists => Self::Exists,
            std::io::ErrorKind::WouldBlock => Self::Busy,
            _ => Self::Unavailable,
        }
    }
}
