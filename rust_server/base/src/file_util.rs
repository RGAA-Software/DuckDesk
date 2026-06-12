use std::{fs, io};

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