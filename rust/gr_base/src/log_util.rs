use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use tracing::level_filters::LevelFilter;
use tracing_log::LogTracer;
use tracing_subscriber::fmt::writer::MakeWriter;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::{fmt, Layer};

const DEFAULT_MAX_FILE_SIZE: u64 = 50 * 1024 * 1024;
const DEFAULT_MAX_FILES: usize = 5;

pub struct LogGuard;

pub fn init_log(path: String, name: String) -> LogGuard {
    let _ = LogTracer::builder().init();

    let fmt_layer = fmt::layer()
        .with_level(true)
        .with_writer(std::io::stdout)
        .with_filter(LevelFilter::INFO);

    let file_writer = RotatingMakeWriter::new(
        PathBuf::from(path),
        normalize_log_filename(&name),
        DEFAULT_MAX_FILE_SIZE,
        DEFAULT_MAX_FILES,
    )
    .expect("init rotating log writer failed");

    let file_layer = fmt::layer()
        .with_ansi(false)
        .with_writer(file_writer)
        .with_filter(LevelFilter::INFO);

    let collector = tracing_subscriber::registry()
        .with(file_layer)
        .with(fmt_layer);

    tracing::subscriber::set_global_default(collector).expect("Tracing collect error");

    LogGuard
}

#[derive(Clone)]
struct RotatingMakeWriter {
    state: Arc<Mutex<RotatingFileState>>,
}

impl RotatingMakeWriter {
    fn new(
        directory: PathBuf,
        file_name: String,
        max_file_size: u64,
        max_files: usize,
    ) -> io::Result<Self> {
        fs::create_dir_all(&directory)?;
        let current_path = directory.join(&file_name);
        let file = open_for_append(&current_path)?;
        let current_size = file.metadata().map(|meta| meta.len()).unwrap_or(0);
        Ok(Self {
            state: Arc::new(Mutex::new(RotatingFileState {
                directory,
                file_name,
                max_file_size,
                max_files,
                file,
                current_size,
            })),
        })
    }
}

impl<'a> MakeWriter<'a> for RotatingMakeWriter {
    type Writer = RotatingWriter;

    fn make_writer(&'a self) -> Self::Writer {
        RotatingWriter {
            state: self.state.clone(),
        }
    }
}

struct RotatingWriter {
    state: Arc<Mutex<RotatingFileState>>,
}

impl Write for RotatingWriter {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let mut state = self.state.lock().expect("log writer lock poisoned");
        state.rotate_if_needed(buf.len() as u64)?;
        let written = state.file.write(buf)?;
        state.current_size += written as u64;
        Ok(written)
    }

    fn flush(&mut self) -> io::Result<()> {
        let mut state = self.state.lock().expect("log writer lock poisoned");
        state.file.flush()
    }
}

struct RotatingFileState {
    directory: PathBuf,
    file_name: String,
    max_file_size: u64,
    max_files: usize,
    file: File,
    current_size: u64,
}

impl RotatingFileState {
    fn rotate_if_needed(&mut self, incoming_bytes: u64) -> io::Result<()> {
        if self.current_size + incoming_bytes <= self.max_file_size {
            return Ok(());
        }

        self.file.flush()?;

        if self.max_files > 1 {
            let last_index = self.max_files - 1;
            let last_path = self.directory.join(rotated_log_filename(&self.file_name, last_index));
            if last_path.exists() {
                let _ = fs::remove_file(&last_path);
            }

            for index in (1..last_index).rev() {
                let src = self.directory.join(rotated_log_filename(&self.file_name, index));
                let dst = self
                    .directory
                    .join(rotated_log_filename(&self.file_name, index + 1));
                if src.exists() {
                    let _ = fs::rename(src, dst);
                }
            }

            let current_path = self.directory.join(&self.file_name);
            if current_path.exists() {
                let first_rotated = self.directory.join(rotated_log_filename(&self.file_name, 1));
                let _ = fs::rename(current_path, first_rotated);
            }
        } else {
            let current_path = self.directory.join(&self.file_name);
            let _ = fs::remove_file(current_path);
        }

        let current_path = self.directory.join(&self.file_name);
        self.file = open_for_append(&current_path)?;
        self.current_size = 0;
        Ok(())
    }
}

fn open_for_append(path: &Path) -> io::Result<File> {
    OpenOptions::new().create(true).append(true).open(path)
}

fn normalize_log_filename(name: &str) -> String {
    let trimmed = name.trim();
    if trimmed.is_empty() {
        return "godesk.log".to_string();
    }
    let path = Path::new(trimmed);
    if path.extension().is_some() {
        trimmed.to_string()
    } else {
        format!("{trimmed}.log")
    }
}

fn rotated_log_filename(file_name: &str, index: usize) -> String {
    let path = Path::new(file_name);
    let stem = path
        .file_stem()
        .and_then(|value| value.to_str())
        .unwrap_or(file_name);
    match path.extension().and_then(|value| value.to_str()) {
        Some(ext) if !ext.is_empty() => format!("{stem}.{index}.{ext}"),
        _ => format!("{file_name}.{index}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalize_log_filename_appends_log_suffix() {
        assert_eq!(normalize_log_filename("godesk_service"), "godesk_service.log");
        assert_eq!(normalize_log_filename("godesk_render_20371.log"), "godesk_render_20371.log");
    }

    #[test]
    fn rotated_log_filename_inserts_index_before_extension() {
        assert_eq!(
            rotated_log_filename("godesk_service.log", 1),
            "godesk_service.1.log"
        );
        assert_eq!(
            rotated_log_filename("godesk_render_20371.log", 4),
            "godesk_render_20371.4.log"
        );
    }
}
