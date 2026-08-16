use service_core::config::USER_PROXY_EXE_NAME;

pub const DEFAULT_RENDER_PORT: u16 = 20371;

pub fn extract_render_port(args: &[String]) -> u16 {
    let mut iter = args.iter().peekable();
    while let Some(arg) = iter.next() {
        let flag = arg.trim_start_matches('-');
        if let Some(value) = flag.strip_prefix("network_listen_port=") {
            return value.parse().unwrap_or(DEFAULT_RENDER_PORT);
        }
        if flag == "network_listen_port" {
            if let Some(value) = iter.peek() {
                return value.parse().unwrap_or(DEFAULT_RENDER_PORT);
            }
        }
    }
    DEFAULT_RENDER_PORT
}

pub fn user_proxy_path(work_dir: &str) -> String {
    format!("{work_dir}/{USER_PROXY_EXE_NAME}")
}

pub fn user_proxy_args(render_port: u16) -> Vec<String> {
    vec![format!("--render-port={render_port}")]
}

#[allow(dead_code)]
pub fn is_user_proxy_process(exe_path: &str) -> bool {
    exe_path
        .rsplit(['\\', '/'])
        .next()
        .map(|name| name.eq_ignore_ascii_case(USER_PROXY_EXE_NAME))
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extract_render_port_from_args() {
        assert_eq!(
            extract_render_port(&["--network_listen_port=20400".to_string()]),
            20400
        );
        assert_eq!(
            extract_render_port(&[
                "--network_listen_port".to_string(),
                "20401".to_string()
            ]),
            20401
        );
        assert_eq!(
            extract_render_port(&["-network_listen_port=20402".to_string()]),
            20402
        );
        assert_eq!(extract_render_port(&[]), DEFAULT_RENDER_PORT);
        assert_eq!(
            extract_render_port(&["--network_listen_port=bad".to_string()]),
            DEFAULT_RENDER_PORT
        );
    }

    #[test]
    fn user_proxy_path_joins_work_dir() {
        assert_eq!(
            user_proxy_path("D:/px"),
            "D:/px/px_function.exe"
        );
    }
}
