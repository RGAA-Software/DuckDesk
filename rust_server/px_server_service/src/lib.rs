#[cfg(windows)]
mod windows_service;

#[cfg(windows)]
pub use windows_service::dispatch;

use std::{collections::HashSet, path::Path};

pub fn load_environment_file(path: &Path) -> Result<(), &'static str> {
    if !path.is_absolute() || path.is_symlink() {
        return Err("service configuration path must be an absolute regular file");
    }
    let contents =
        std::fs::read_to_string(path).map_err(|_| "service configuration unavailable")?;
    let mut seen_names = HashSet::new();
    for line in contents.lines() {
        let setting = line.trim();
        if setting.is_empty() || setting.starts_with('#') {
            continue;
        }
        let (name, raw_value) = setting
            .split_once('=')
            .ok_or("service configuration field invalid")?;
        if !name.starts_with("PIXELS_")
            || !name.bytes().all(|character| {
                character.is_ascii_uppercase() || character.is_ascii_digit() || character == b'_'
            })
            || !seen_names.insert(name.to_owned())
            || raw_value.contains('\0')
        {
            return Err("service configuration field invalid");
        }
        let value = if raw_value.len() >= 2
            && ((raw_value.starts_with('\'') && raw_value.ends_with('\''))
                || (raw_value.starts_with('"') && raw_value.ends_with('"')))
        {
            &raw_value[1..raw_value.len() - 1]
        } else {
            raw_value
        };
        std::env::set_var(name, value);
    }
    Ok(())
}
