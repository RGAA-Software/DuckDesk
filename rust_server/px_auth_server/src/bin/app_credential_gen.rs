//! 生成接入方凭据（appkey/app_secret）的小工具。
//!
//! 用法：
//!   app_credential_gen                 # 只打印，不改文件
//!   app_credential_gen --write         # 写入 ./px_auth.toml
//!   app_credential_gen --write --file /opt/px_auth_server/px_auth.toml
//!   app_credential_gen --require true  # 配合 --write，同时设置 require_app_credential
//!
//! 生成后需重启 px_auth_server 生效。

use ring::rand::{SecureRandom, SystemRandom};

const DEFAULT_SETTINGS_PATH: &str = "px_auth.toml";

fn hex_encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn gen_pair() -> (String, String) {
    let rng = SystemRandom::new();
    let mut appkey = [0u8; 16];
    let mut secret = [0u8; 32];
    rng.fill(&mut appkey).expect("rng appkey");
    rng.fill(&mut secret).expect("rng app_secret");
    (hex_encode(&appkey), hex_encode(&secret))
}

/// Upsert `[app_credential]` 段（保留文件其余内容），并按需设置 require_app_credential。
fn upsert_settings(
    content: &str,
    appkey: &str,
    app_secret: &str,
    require: Option<bool>,
) -> String {
    let mut out = String::with_capacity(content.len() + 256);
    let mut in_cred_section = false;
    let mut wrote_section = false;
    let mut wrote_require = require.is_none(); // 不要求写时视为已完成
    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') {
            in_cred_section = trimmed == "[app_credential]";
            out.push_str(line);
            out.push('\n');
            if in_cred_section {
                wrote_section = true;
                out.push_str(&format!("appkey = \"{appkey}\"\napp_secret = \"{app_secret}\"\n"));
            }
            continue;
        }
        if trimmed.starts_with("require_app_credential") {
            // 该键是顶层键：无论出现在哪个段内都丢弃原行，由统一逻辑写/补。
            if require.is_some() {
                continue;
            }
        }
        if in_cred_section {
            // 丢弃旧段内的 appkey/app_secret 行（其他行保留）
            if trimmed.starts_with("appkey") || trimmed.starts_with("app_secret") {
                continue;
            }
        }
        out.push_str(line);
        out.push('\n');
    }
    if !wrote_section {
        out.push_str(&format!(
            "\n[app_credential]\nappkey = \"{appkey}\"\napp_secret = \"{app_secret}\"\n"
        ));
    }
    if let Some(req) = require {
        if !wrote_require {
            // 顶层键必须在所有 [table] 之前，插到文件开头。
            out = format!("require_app_credential = {req}\n{out}");
        }
    }
    out
}

fn main() {
    let mut write = false;
    let mut file = DEFAULT_SETTINGS_PATH.to_string();
    let mut require: Option<bool> = None;
    let mut explicit_appkey: Option<String> = None;
    let mut explicit_secret: Option<String> = None;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--write" => write = true,
            "--file" => {
                file = args.next().unwrap_or_else(|| {
                    eprintln!("--file requires a path");
                    std::process::exit(2);
                });
            }
            "--require" => {
                let v = args.next().unwrap_or_else(|| {
                    eprintln!("--require requires true|false");
                    std::process::exit(2);
                });
                require = match v.as_str() {
                    "true" => Some(true),
                    "false" => Some(false),
                    _ => {
                        eprintln!("--require requires true|false");
                        std::process::exit(2);
                    }
                };
            }
            "--appkey" => {
                explicit_appkey = Some(args.next().unwrap_or_else(|| {
                    eprintln!("--appkey requires a value");
                    std::process::exit(2);
                }));
            }
            "--secret" => {
                explicit_secret = Some(args.next().unwrap_or_else(|| {
                    eprintln!("--secret requires a value");
                    std::process::exit(2);
                }));
            }
            "-h" | "--help" => {
                println!("Usage: app_credential_gen [--write] [--file PATH] [--require true|false] [--appkey HEX] [--secret HEX]");
                return;
            }
            other => {
                eprintln!("unknown argument: {other}");
                std::process::exit(2);
            }
        }
    }

    // 显式指定凭据时两边必须同时给（用于把客户端内嵌的凭据写入服务端配置）。
    let (appkey, app_secret) = match (explicit_appkey, explicit_secret) {
        (Some(k), Some(s)) => (k, s),
        (None, None) => gen_pair(),
        _ => {
            eprintln!("--appkey and --secret must be provided together");
            std::process::exit(2);
        }
    };

    if !write {
        println!("[app_credential]");
        println!("appkey = \"{appkey}\"");
        println!("app_secret = \"{app_secret}\"");
        println!("\n加 --write 写入 {file}，然后重启 px_auth_server。");
        return;
    }

    let content = std::fs::read_to_string(&file).unwrap_or_default();
    let updated = upsert_settings(&content, &appkey, &app_secret, require);
    std::fs::write(&file, &updated).expect("write settings");
    println!("已写入 {file}");
    println!("appkey = \"{appkey}\"");
    println!("请重启 px_auth_server 使配置生效。");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upsert_appends_section_when_missing() {
        let out = upsert_settings("server_port = 30400\n", "AA", "BB", None);
        assert!(out.contains("[app_credential]"));
        assert!(out.contains("appkey = \"AA\""));
        assert!(out.contains("app_secret = \"BB\""));
        assert!(out.contains("server_port = 30400"));
    }

    #[test]
    fn upsert_replaces_existing_section() {
        let content = "server_port = 1\n\n[app_credential]\nappkey = \"OLD\"\napp_secret = \"OLD\"\n\n[bootstrap]\njwt_secret = \"x\"\n";
        let out = upsert_settings(content, "AA", "BB", None);
        assert!(!out.contains("OLD"));
        assert!(out.contains("jwt_secret = \"x\""));
        assert_eq!(out.matches("appkey = \"AA\"").count(), 1);
        assert_eq!(out.matches("app_secret = \"BB\"").count(), 1);
    }

    #[test]
    fn upsert_sets_require_flag() {
        let content = "require_app_credential = false\nserver_port = 1\n";
        let out = upsert_settings(content, "AA", "BB", Some(true));
        assert!(out.contains("require_app_credential = true"));
        assert!(!out.contains("require_app_credential = false"));
    }

    #[test]
    fn upsert_new_require_flag_goes_to_top_level() {
        let content = "server_port = 1\n\n[bootstrap]\njwt_secret = \"x\"\n";
        let out = upsert_settings(content, "AA", "BB", Some(false));
        // 顶层键必须在 [table] 之前
        let flag_pos = out.find("require_app_credential = false").unwrap();
        let table_pos = out.find("[app_credential]").unwrap();
        let bootstrap_pos = out.find("[bootstrap]").unwrap();
        assert!(flag_pos < bootstrap_pos && flag_pos < table_pos);
    }

    #[test]
    fn stray_require_inside_section_is_removed() {
        let content = "server_port = 1\n\n[app_credential]\nappkey = \"OLD\"\nrequire_app_credential = false\n";
        let out = upsert_settings(content, "AA", "BB", Some(false));
        let flag_pos = out.find("require_app_credential = false").unwrap();
        let table_pos = out.find("[app_credential]").unwrap();
        assert!(flag_pos < table_pos, "flag must be top-level, got:\n{out}");
    }
}
