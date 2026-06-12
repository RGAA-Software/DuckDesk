use std::sync::Arc;
use tokio::sync::Mutex;
use std::fs;
use std::path::Path;
use chrono::prelude::*;
use crate::gSpvrSettings;

pub struct SpvrSystemManager {

}

impl SpvrSystemManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub fn cal_logs_size(&self) -> i64 {
        gr_base::calculate_dir_size("./logs".to_string())
    }

    pub fn cal_logs_size_readable(&self) -> String {
        let size = self.cal_logs_size();
        gr_base::format_file_size(size)
    }

    pub fn cal_upload_size(&self) -> i64 {
        gr_base::calculate_dir_size("./uploads".to_string())
    }

    pub fn cal_upload_size_readable(&self) -> String {
        let size = self.cal_upload_size();
        gr_base::format_file_size(size)
    }

    pub fn cal_data_size(&self) -> i64 {
        self.cal_logs_size() + self.cal_upload_size()
    }

    pub fn cal_data_size_readable(&self) -> String {
        let size = self.cal_data_size();
        gr_base::format_file_size(size)
    }

    pub async fn clear_data(&self) {
        // 1. clear all uploaded logs
        let logs_path = gSpvrSettings
            .lock().await.abs_upload_logs_path.clone();

        if let Err(e)  = gr_base::clear_directory(logs_path.as_str()) {
            tracing::error!("clear uploaded logs failed: {:?}", e);
        }

        // 2. clear logs except logging now
        let app_logs_path = "./logs";
        if let Err(e) = self.delete_logs_except_today(app_logs_path) {
            tracing::error!("delete logs failed: {:?}", e);
        }
    }

    fn delete_logs_except_today(&self, dir_path: &str) -> Result<u64, std::io::Error> {
        let current_date = Local::now().format("%Y-%m-%d").to_string();
        let mut deleted_count = 0;

        fn delete_recursive(path: &Path, current_date: &str, deleted_count: &mut u64) -> Result<(), std::io::Error> {
            if path.is_dir() {
                let entries = fs::read_dir(path)?;

                for entry in entries {
                    let entry = entry?;
                    let entry_path = entry.path();

                    if entry_path.is_dir() {
                        // 递归处理子目录
                        delete_recursive(&entry_path, current_date, deleted_count)?;
                    }
                    else if entry_path.is_file() {
                        // 检查文件名是否包含当前日期
                        let file_name = entry_path.file_name()
                            .unwrap_or_default()
                            .to_string_lossy();

                        if file_name.contains(current_date) {
                            tracing::info!("跳过文件 (包含今天日期): {:?}", entry_path);
                        }
                        else {
                            fs::remove_file(&entry_path)?;
                            *deleted_count += 1;
                            tracing::info!("删除文件: {:?}", entry_path);
                        }
                    }
                }
            }
            Ok(())
        }

        let path = Path::new(dir_path);
        if !path.exists() || !path.is_dir() {
            return Ok(0);
        }

        delete_recursive(path, &current_date, &mut deleted_count)?;
        tracing::info!("成功删除 {} 个文件，跳过包含今天日期({})的文件", deleted_count, current_date);
        Ok(deleted_count)
    }

}