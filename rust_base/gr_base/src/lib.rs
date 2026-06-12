pub mod crypto_util;
pub mod csv_util;
pub mod email_util;
pub mod file_util;
pub mod hash_util;
pub mod hwid_util;
pub mod ip_util;
pub mod json_util;
pub mod kv_storage;
pub mod log_util;
pub mod mongodb_util;
pub mod redis_util;
pub mod server_id_util;
pub mod string_util;
pub mod sys_info;

use crate::hash_util::{compute_hash, HashAlgo};
use chrono::{DateTime, Duration, Local, TimeZone};
use serde::{Deserialize, Deserializer, Serialize};
use std::collections::HashMap;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};
use std::{fs, io};

pub type StringMap = HashMap<String, String>;
pub type StrMap = HashMap<&'static str, String>;
pub type RespStringMap = RespMessage<StringMap>;
pub type RespVecStringMap = RespMessage<Vec<StringMap>>;

#[derive(Serialize, Debug, Deserialize)]
pub struct RespMessage<T>
where
    T: Serialize,
    T: Default,
{
    pub code: i32,
    pub message: String,
    pub timestamp: i64,
    pub data: T,
}

pub struct RespMsgPair {
    pub code: i32,
    pub message: String,
}

impl<T> RespMessage<T>
where
    T: Serialize,
    T: Default,
{
    pub fn new_data(code: i32, message: String, data: T) -> Self {
        Self {
            code,
            message: message.to_string(),
            timestamp: get_current_timestamp(),
            data,
        }
    }

    pub fn new_message(code: i32, message: String, data: T) -> Self {
        Self {
            code,
            message,
            timestamp: get_current_timestamp(),
            data,
        }
    }

    pub fn new(code: i32) -> Self {
        RespMessage::<T>::new_message(code, String::new(), T::default())
    }

    pub fn new_pair(pair: RespMsgPair) -> Self {
        RespMessage::<T>::new_message(pair.code, pair.message, T::default())
    }

    pub fn ok() -> Self {
        RespMessage::<T>::new_message(200, "ok".to_string(), T::default())
    }

    pub fn ok_str(msg: String) -> Self {
        RespMessage::<T>::new_message(200, msg, T::default())
    }
}

pub fn ok_resp<T>(value: T) -> RespMessage<T>
where
    T: Serialize,
    T: Default,
{
    RespMessage::<T> {
        code: 200,
        message: "ok".to_string(),
        timestamp: get_current_timestamp(),
        data: value,
    }
}

pub fn resp_empty_str(pair: RespMsgPair) -> RespMessage<String> {
    RespMessage::<String> {
        code: pair.code,
        message: pair.message,
        timestamp: get_current_timestamp(),
        data: String::new(),
    }
}

pub fn resp_empty_str_map(pair: RespMsgPair) -> RespStringMap {
    RespMessage::<StringMap> {
        code: pair.code,
        message: pair.message,
        timestamp: get_current_timestamp(),
        data: StringMap::new(),
    }
}

pub fn resp_empty_vec_str_map(pair: RespMsgPair) -> RespVecStringMap {
    RespMessage::<Vec<StringMap>> {
        code: pair.code,
        message: pair.message,
        timestamp: get_current_timestamp(),
        data: Vec::new(),
    }
}

pub fn ok_resp_str(data: String) -> RespMessage<String> {
    RespMessage::<String> {
        code: 200,
        message: "ok".to_string(),
        timestamp: get_current_timestamp(),
        data,
    }
}

pub fn ok_resp_str_map(data: HashMap<String, String>) -> RespStringMap {
    RespMessage::<StringMap> {
        code: 200,
        message: "ok".to_string(),
        timestamp: get_current_timestamp(),
        data,
    }
}

pub fn ok_resp_vec_str_map(data: Vec<HashMap<String, String>>) -> RespVecStringMap {
    RespMessage::<Vec<StringMap>> {
        code: 0,
        message: "".to_string(),
        timestamp: get_current_timestamp(),
        data,
    }
}

pub fn get_query_param(params: &HashMap<String, String>, key: &str) -> Option<String> {
    let value = params.get(key);
    if let Some(value) = value {
        Some(value.to_string())
    } else {
        None
    }
}

pub fn get_current_timestamp() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis() as i64
}

pub fn get_current_readable_timestamp() -> String {
    let ts = get_current_timestamp();
    format_readable_timestamp(ts)
}

pub fn format_readable_timestamp(timestamp: i64) -> String {
    let datetime: DateTime<Local> = Local.timestamp_opt(timestamp / 1000, 0).unwrap();
    datetime.format("%Y-%m-%d %H:%M:%S").to_string()
}

pub fn get_current_date() -> String {
    let now: DateTime<Local> = Local::now();
    now.format("%Y-%m-%d").to_string()
}

pub fn md5_hex(input: &String) -> String {
    compute_hash(HashAlgo::MD5, input.as_bytes())
}

pub fn current_exe_dir() -> String {
    let current_dir = std::env::current_exe().unwrap();
    let current_dir = current_dir.parent().unwrap();
    let path = current_dir.to_str().unwrap().to_string();
    path.replace("\\", "/")
}

pub fn create_dir_if_not_exists(path: &str) -> io::Result<()> {
    match fs::create_dir(path) {
        Ok(_) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(()),
        Err(e) => Err(e),
    }
}

pub fn create_dir_all_if_not_exists(path: &str) -> io::Result<()> {
    match fs::create_dir_all(path) {
        Ok(_) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(()),
        Err(e) => Err(e),
    }
}

// abc.txt -> txt
// abc.TXT -> txt
pub fn get_extension(path: &str) -> Result<String, String> {
    let path = Path::new(path);
    let extension = path
        .extension()
        .and_then(|ext| ext.to_str())
        .ok_or("Cannot determine extension")?;
    Ok(extension.to_lowercase())
}

pub fn serde_as_bool<'de, D>(deserializer: D) -> Result<bool, D::Error>
where
    D: Deserializer<'de>,
{
    let val = serde_json::Value::deserialize(deserializer)?;
    match val {
        serde_json::Value::Bool(b) => Ok(b),
        serde_json::Value::String(s) => {
            let s_lower = s.to_lowercase();
            Ok(s_lower == "true" || s_lower == "1" || s_lower == "yes")
        }
        serde_json::Value::Number(num) => Ok(num.as_i64().unwrap_or(0) != 0),
        _ => Ok(false),
    }
}

/// 递归统计目录大小
pub fn calculate_dir_size(dir_path: String) -> i64 {
    let path = Path::new(&dir_path);

    if !path.exists() {
        eprintln!("目录不存在: {}", dir_path);
        return 0;
    }

    if !path.is_dir() {
        eprintln!("路径不是目录: {}", dir_path);
        return 0;
    }

    calculate_size_recursive(path)
}

fn calculate_size_recursive(path: &Path) -> i64 {
    let mut total_size: i64 = 0;

    // 读取目录内容，如果出错就返回0
    let entries = match fs::read_dir(path) {
        Ok(entries) => entries,
        Err(e) => {
            println!("e: {}", e);
            return 0;
        }
    };

    for entry in entries {
        let entry = match entry {
            Ok(entry) => entry,
            Err(e) => {
                println!("-> e: {}", e);
                continue;
            } // 跳过无法访问的条目
        };

        let entry_path = entry.path();
        let metadata = match fs::metadata(&entry_path) {
            Ok(meta) => meta,
            Err(e) => {
                println!("* e: {}", e);
                continue;
            } // 跳过无法获取元数据的条目
        };

        if metadata.is_dir() {
            // 递归计算子目录大小，出错则按0处理
            total_size += calculate_size_recursive(&entry_path);
        } else {
            // 文件大小，出错则按0处理
            //println!("add file size: {}->{}", entry_path.display(), metadata.len());
            total_size += metadata.len() as i64;
        }
    }

    total_size
}

// FROM: 169224568
// TO: 161.39 MB
pub fn format_file_size(bytes: i64) -> String {
    const KB: f64 = 1024.0;
    const MB: f64 = KB * 1024.0;
    const GB: f64 = MB * 1024.0;

    if bytes >= GB as i64 {
        format!("{:.2} GB", bytes as f64 / GB)
    } else if bytes >= MB as i64 {
        format!("{:.2} MB", bytes as f64 / MB)
    } else if bytes >= KB as i64 {
        format!("{:.2} KB", bytes as f64 / KB)
    } else {
        format!("{} B", bytes)
    }
}

pub fn clear_directory(dir_path: &str) -> Result<(), std::io::Error> {
    let path = Path::new(dir_path);

    // 检查路径是否存在且是目录
    if !path.exists() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::NotFound,
            format!("目录不存在: {}", dir_path),
        ));
    }

    if !path.is_dir() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            format!("路径不是目录: {}", dir_path),
        ));
    }

    // 读取目录内容
    let entries = fs::read_dir(path)?;

    for entry in entries {
        let entry = entry?;
        let entry_path = entry.path();

        if entry_path.is_file() {
            // 删除文件
            fs::remove_file(&entry_path)?;
            println!("删除文件: {:?}", entry_path);
        } else if entry_path.is_dir() {
            // 删除子目录及其内容
            fs::remove_dir_all(&entry_path)?;
            println!("删除目录: {:?}", entry_path);
        }
    }

    println!("成功清空目录: {}", dir_path);
    Ok(())
}

/// Alternative: Compact format like "1d 2h 3m 4s"
pub fn format_duration_compact(milliseconds: i64) -> String {
    let duration = Duration::milliseconds(milliseconds);
    let total_seconds = duration.num_seconds();

    let years = total_seconds / 31_536_000;
    let days = (total_seconds % 31_536_000) / 86_400;
    let hours = (total_seconds % 86_400) / 3_600;
    let minutes = (total_seconds % 3_600) / 60;
    let seconds = total_seconds % 60;

    let mut parts = Vec::new();

    if years > 0 {
        parts.push(format!("{}y", years));
    }
    if days > 0 {
        parts.push(format!("{}d", days));
    }
    if hours > 0 {
        parts.push(format!("{}h", hours));
    }
    if minutes > 0 {
        parts.push(format!("{}m", minutes));
    }
    if seconds > 0 {
        parts.push(format!("{}s", seconds));
    }

    if parts.is_empty() {
        "0s".to_string()
    } else {
        parts.join(" ")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn get_test_debug_directory() -> String {
        let current_dir = std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
        let dir = current_dir.join("target").join("debug");
        dir.to_str().unwrap().to_string()
    }

    #[test]
    fn md5_test() {
        let result = md5_hex(&"123".to_string());
        println!("{}", result);
        assert_eq!(result, "202cb962ac59075b964b07152d234b70");
    }

    #[test]
    fn test_extension() {
        let result = get_extension("abc.txt").unwrap();
        println!("{}", result);
        let result = get_extension("abc.TXT").unwrap();
        println!("{}", result);
    }

    #[test]
    fn test_readable_timestamp() {
        let ts = get_current_readable_timestamp();
        println!("{}", ts);
    }

    #[test]
    fn test_calculate_dir_size() {
        let mut dir_path = std::env::current_dir()
            .unwrap()
            .to_str()
            .unwrap()
            .to_string();
        println!("--> {}", dir_path);
        let size = calculate_dir_size(dir_path.clone() + "/../target/debug/logs");
        println!("logs size: {}", size);

        let size = calculate_dir_size(dir_path + "/../target/debug/uploads");
        println!("uploads size: {}", size);
        println!("uploads size: {}", format_file_size(size));
    }

    #[test]
    fn test_format_time() {
        println!("{}", format_duration_compact(31200000));
    }
}
