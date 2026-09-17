//! Display-file metadata validation, not a filesystem containment boundary.
//! Storage adapters use controlled UUID paths and separately reject reparse/symlink escapes.
use crate::StoreError;

pub(crate) fn valid_basename(name: &str) -> Result<(), StoreError> {
    if name.is_empty()
        || name.len() > 255
        || name.trim() != name
        || name.ends_with('.')
        || name
            .chars()
            .any(|c| c.is_control() || "/\\:*?\"<>|".contains(c))
    {
        return Err(StoreError::InvalidInput);
    }
    let stem = name
        .split('.')
        .next()
        .unwrap_or_default()
        .to_ascii_uppercase();
    if matches!(
        stem.as_str(),
        "CON"
            | "PRN"
            | "AUX"
            | "NUL"
            | "CONIN$"
            | "CONOUT$"
            | "COM¹"
            | "COM²"
            | "COM³"
            | "LPT¹"
            | "LPT²"
            | "LPT³"
    ) || (stem.len() == 4
        && (stem.starts_with("COM") || stem.starts_with("LPT"))
        && matches!(stem.as_bytes()[3], b'1'..=b'9'))
    {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}
