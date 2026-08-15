pub mod coordinator;
pub mod stream;

pub use coordinator::{VirtualFileCoordinator, VirtualFileSession};
pub use stream::{ReadChunkRequest, RespBufferData, StreamError, VirtualFileStreamCore};

#[cfg(windows)]
pub mod win_clipboard;

#[cfg(windows)]
pub use win_clipboard::install_virtual_file_clipboard;
