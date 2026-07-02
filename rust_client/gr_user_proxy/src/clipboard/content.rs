use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ClipboardFileEntry {
    pub full_path: String,
    pub file_name: String,
    pub ref_path: String,
    pub total_size: i64,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ClipboardContent {
    pub text: Option<String>,
    pub files: Vec<ClipboardFileEntry>,
}

impl ClipboardContent {
    pub fn has_text(&self) -> bool {
        self.text.as_ref().is_some_and(|text| !text.is_empty())
    }

    pub fn has_files(&self) -> bool {
        !self.files.is_empty()
    }

    pub fn is_empty(&self) -> bool {
        !self.has_text() && !self.has_files()
    }

    pub fn fingerprint(&self) -> String {
        let mut parts = Vec::new();
        if let Some(text) = &self.text {
            parts.push(format!("t:{text}"));
        }
        for file in &self.files {
            parts.push(format!(
                "f:{}:{}:{}",
                file.full_path, file.ref_path, file.total_size
            ));
        }
        parts.join("|")
    }
}

/// Echo signature for a file list; must be identical between the entries
/// applied from remote and the entries later read back by the local poller.
pub fn files_signature(files: &[ClipboardFileEntry]) -> String {
    files
        .iter()
        .map(|file| format!("{}:{}", file.full_path, file.total_size))
        .collect::<Vec<_>>()
        .join("|")
}

pub fn build_file_entries_from_paths(full_paths: &[String]) -> Vec<ClipboardFileEntry> {
    if full_paths.is_empty() {
        return Vec::new();
    }

    let expanded = expand_paths(full_paths);
    if expanded.is_empty() {
        return Vec::new();
    }

    let base_folder = resolve_base_folder(full_paths, &expanded);
    if base_folder.as_os_str().is_empty() {
        return Vec::new();
    }

    expanded
        .into_iter()
        .filter_map(|path| make_file_entry(&base_folder, &path))
        .collect()
}

fn expand_paths(full_paths: &[String]) -> Vec<PathBuf> {
    let mut out = Vec::new();
    for raw in full_paths {
        let path = PathBuf::from(raw);
        if path.is_dir() {
            collect_files_recursive(&path, &mut out);
        } else if path.is_file() {
            out.push(path);
        }
    }
    out
}

fn collect_files_recursive(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(read_dir) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in read_dir.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_files_recursive(&path, out);
        } else if path.is_file() {
            out.push(path);
        }
    }
}

fn resolve_base_folder(full_paths: &[String], expanded: &[PathBuf]) -> PathBuf {
    for raw in full_paths {
        let path = PathBuf::from(raw);
        if path.is_dir() {
            return path;
        }
    }
    expanded
        .first()
        .and_then(|path| path.parent().map(Path::to_path_buf))
        .unwrap_or_default()
}

fn make_file_entry(base_folder: &Path, full_path: &Path) -> Option<ClipboardFileEntry> {
    if !full_path.exists() {
        return None;
    }
    let full_path = full_path.canonicalize().unwrap_or_else(|_| full_path.to_path_buf());
    let base_folder = base_folder
        .canonicalize()
        .unwrap_or_else(|_| base_folder.to_path_buf());
    let full_u8 = path_to_forward_slashes(&full_path);
    let base_u8 = path_to_forward_slashes(&base_folder);
    if !full_u8.starts_with(&base_u8) {
        tracing::error!(
            "clipboard file not under base folder, base={}, file={}",
            base_u8,
            full_u8
        );
        return None;
    }

    let mut ref_path = full_u8[base_u8.len()..].to_string();
    while ref_path.starts_with('/') || ref_path.starts_with('\\') {
        ref_path.remove(0);
    }

    let total_size = std::fs::metadata(&full_path).map(|meta| meta.len() as i64).unwrap_or(0);
    Some(ClipboardFileEntry {
        full_path: full_u8,
        file_name: full_path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or_default()
            .to_string(),
        ref_path,
        total_size,
    })
}

fn path_to_forward_slashes(path: &Path) -> String {
    path.display().to_string().replace('\\', "/")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_dir(name: &str) -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time")
            .as_nanos();
        std::env::temp_dir().join(format!("gr_user_proxy_{name}_{stamp}"))
    }

    #[test]
    fn build_file_entries_from_single_file() {
        let root = temp_dir("single");
        fs::create_dir_all(&root).expect("mkdir");
        let file = root.join("hello.txt");
        fs::write(&file, b"abc").expect("write");
        let entries = build_file_entries_from_paths(&[file.display().to_string()]);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].file_name, "hello.txt");
        assert_eq!(entries[0].total_size, 3);
    }

    #[test]
    fn fingerprint_changes_with_text_and_files() {
        let mut content = ClipboardContent::default();
        assert_eq!(content.fingerprint(), "");
        content.text = Some("a".to_string());
        assert_ne!(content.fingerprint(), "");
    }
}
