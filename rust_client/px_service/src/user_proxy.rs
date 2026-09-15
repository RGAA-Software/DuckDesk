use service_core::config::USER_PROXY_EXE_NAME;

pub fn extract_render_port(args: &[String]) -> Result<u16, String> {
    let mut iter = args.iter().peekable();
    while let Some(arg) = iter.next() {
        let flag = arg.trim_start_matches('-');
        if let Some(value) = flag.strip_prefix("network_listen_port=") {
            return value
                .parse()
                .map_err(|_| "invalid --network_listen_port value".to_string());
        }
        if flag == "network_listen_port" {
            let value = iter
                .peek()
                .ok_or_else(|| "missing --network_listen_port value".to_string())?;
            return value
                .parse()
                .map_err(|_| "invalid --network_listen_port value".to_string());
        }
    }
    Err("missing --network_listen_port".to_string())
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
            Ok(20400)
        );
        assert_eq!(
            extract_render_port(&["--network_listen_port".to_string(), "20401".to_string()]),
            Ok(20401)
        );
        assert_eq!(
            extract_render_port(&["-network_listen_port=20402".to_string()]),
            Ok(20402)
        );
        assert!(extract_render_port(&[]).is_err());
        assert!(extract_render_port(&["--network_listen_port=bad".to_string()]).is_err());
        assert!(extract_render_port(&["--network_listen_port".to_string()]).is_err());
    }

    #[test]
    fn user_proxy_path_joins_work_dir() {
        assert_eq!(user_proxy_path("D:/px"), "D:/px/px_function.exe");
    }
}
