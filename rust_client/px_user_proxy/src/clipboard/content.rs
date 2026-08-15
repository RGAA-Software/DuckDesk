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

/// Build clipboard metadata from absolute paths (matches `tc::clipboard::BuildFileEntriesFromPaths`
/// with per-folder `ref_path` prefix like `fs_object`).
pub fn build_file_entries_from_paths(full_paths: &[String]) -> Vec<ClipboardFileEntry> {
    if full_paths.is_empty() {
        return Vec::new();
    }

    let roots: Vec<PathBuf> = full_paths.iter().map(PathBuf::from).collect();
    let has_directory = roots.iter().any(|path| path.is_dir());

    if has_directory {
        let mut entries = Vec::new();
        for root in &roots {
            if root.is_dir() {
                append_directory_entries(root, &mut entries);
            } else if root.is_file() {
                if let Some(entry) = make_file_entry(root, &file_name_ref(root)) {
                    entries.push(entry);
                }
            }
        }
        return entries;
    }

    build_file_only_entries(&roots)
}

fn build_file_only_entries(roots: &[PathBuf]) -> Vec<ClipboardFileEntry> {
    let mut expanded = Vec::new();
    for root in roots {
        if root.is_file() {
            expanded.push(root.clone());
        }
    }
    if expanded.is_empty() {
        return Vec::new();
    }

    let base_folder = expanded
        .first()
        .and_then(|path| path.parent().map(Path::to_path_buf))
        .unwrap_or_default();

    expanded
        .into_iter()
        .filter_map(|path| {
            let ref_path = relative_ref_path(&base_folder, &path)?;
            make_file_entry(&path, &ref_path)
        })
        .collect()
}

fn append_directory_entries(dir: &Path, out: &mut Vec<ClipboardFileEntry>) {
    let folder_name = dir
        .file_name()
        .and_then(|name| name.to_str())
        .filter(|name| !name.is_empty());
    let Some(folder_name) = folder_name else {
        return;
    };

    let mut files = Vec::new();
    collect_files_recursive(dir, &mut files);
    let base = dir.canonicalize().unwrap_or_else(|_| dir.to_path_buf());

    for file in files {
        let Some(rel) = relative_ref_path(&base, &file) else {
            continue;
        };
        let ref_path = if rel.is_empty() {
            folder_name.to_string()
        } else {
            format!("{folder_name}/{rel}")
        };
        if let Some(entry) = make_file_entry(&file, &ref_path) {
            out.push(entry);
        }
    }
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

fn file_name_ref(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .unwrap_or_default()
        .to_string()
}

fn relative_ref_path(base_folder: &Path, full_path: &Path) -> Option<String> {
    let full = full_path.canonicalize().unwrap_or_else(|_| full_path.to_path_buf());
    let base = base_folder
        .canonicalize()
        .unwrap_or_else(|_| base_folder.to_path_buf());
    let full_u8 = path_to_forward_slashes(&full);
    let base_u8 = path_to_forward_slashes(&base);
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
    Some(ref_path)
}

fn make_file_entry(full_path: &Path, ref_path: &str) -> Option<ClipboardFileEntry> {
    if !full_path.exists() {
        return None;
    }
    let total_size = std::fs::metadata(full_path)
        .map(|meta| meta.len() as i64)
        .unwrap_or(0);
    Some(ClipboardFileEntry {
        full_path: path_to_forward_slashes(
            &full_path.canonicalize().unwrap_or_else(|_| full_path.to_path_buf()),
        ),
        file_name: file_name_ref(full_path),
        ref_path: ref_path.to_string(),
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
        std::env::temp_dir().join(format!("px_user_proxy_{name}_{stamp}"))
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
        assert_eq!(entries[0].ref_path, "hello.txt");
        assert_eq!(entries[0].total_size, 3);
    }

    #[test]
    fn build_file_entries_preserves_directory_structure() {
        let root = temp_dir("tree");
        let bundle = root.join("bundle");
        fs::create_dir_all(bundle.join("nested")).expect("mkdir");
        fs::write(bundle.join("top.txt"), b"1").expect("write");
        fs::write(bundle.join("nested").join("inner.txt"), b"22").expect("write");

        let entries = build_file_entries_from_paths(&[bundle.display().to_string()]);
        assert_eq!(entries.len(), 2);

        let mut top = false;
        let mut inner = false;
        for entry in &entries {
            if entry.file_name == "top.txt" {
                top = true;
                assert_eq!(entry.ref_path, "bundle/top.txt");
            }
            if entry.file_name == "inner.txt" {
                inner = true;
                assert_eq!(entry.ref_path, "bundle/nested/inner.txt");
            }
        }
        assert!(top && inner);
    }

    #[test]
    fn build_file_entries_mixed_directory_and_sibling_files() {
        let root = temp_dir("mixed");
        let bundle = root.join("bundle");
        fs::create_dir_all(bundle.join("nested")).expect("mkdir");
        fs::write(bundle.join("nested").join("in.txt"), b"x").expect("write");
        let loose = root.join("sibling.txt");
        fs::write(&loose, b"y").expect("write");

        let entries = build_file_entries_from_paths(&[
            bundle.display().to_string(),
            loose.display().to_string(),
        ]);
        assert_eq!(entries.len(), 2);

        let mut found_in = false;
        let mut found_sibling = false;
        for entry in &entries {
            if entry.file_name == "in.txt" {
                found_in = true;
                assert_eq!(entry.ref_path, "bundle/nested/in.txt");
            }
            if entry.file_name == "sibling.txt" {
                found_sibling = true;
                assert_eq!(entry.ref_path, "sibling.txt");
            }
        }
        assert!(found_in && found_sibling);
    }

    #[test]
    fn build_file_entries_multiple_files_same_folder() {
        let root = temp_dir("multi");
        fs::create_dir_all(root.join("sub")).expect("mkdir");
        let a = root.join("a.txt");
        let b = root.join("sub").join("b.txt");
        fs::write(&a, b"a").expect("write");
        fs::write(&b, b"b").expect("write");

        let entries = build_file_entries_from_paths(&[a.display().to_string(), b.display().to_string()]);
        assert_eq!(entries.len(), 2);
        let refs: Vec<_> = entries.iter().map(|e| e.ref_path.as_str()).collect();
        assert!(refs.contains(&"a.txt"));
        assert!(refs.contains(&"sub/b.txt"));
    }

    #[test]
    fn fingerprint_changes_with_text_and_files() {
        let mut content = ClipboardContent::default();
        assert_eq!(content.fingerprint(), "");
        content.text = Some("a".to_string());
        assert_ne!(content.fingerprint(), "");
    }
}
