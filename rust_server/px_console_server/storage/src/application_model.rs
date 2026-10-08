use crate::StoreError;
use serde::{Deserialize, Serialize};
use url::{Host, Url};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ApplicationAccess {
    Public,
    Acl,
}
impl ApplicationAccess {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Public => "public",
            Self::Acl => "acl",
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VideoCodec {
    H264,
    H265,
}
impl VideoCodec {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::H264 => "h264",
            Self::H265 => "h265",
        }
    }
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct VideoSpec {
    pub codec: VideoCodec,
    pub bitrate_kbps: u32,
}
// RDP has no capture/encoder or Windows application launch settings. Its existing workspace
// is preserved; GameHook/WebView continue in the node's current user environment.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ApplicationLaunch {
    GameHook {
        executable_path: String,
        arguments: String,
        video: VideoSpec,
    },
    Webview {
        entry_url: String,
        video: VideoSpec,
    },
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Rdp,
}
impl ApplicationLaunch {
    pub(crate) fn kind(&self) -> &'static str {
        match self {
            Self::GameHook { .. } => "game_hook",
            Self::Webview { .. } => "webview",
            Self::Rdp => "rdp",
        }
    }
    pub(crate) fn video(&self) -> Option<&VideoSpec> {
        match self {
            Self::GameHook { video, .. } | Self::Webview { video, .. } => Some(video),
            Self::Rdp => None,
        }
    }
    pub(crate) fn executable(&self) -> Option<&str> {
        match self {
            Self::GameHook {
                executable_path, ..
            } => Some(executable_path),
            _ => None,
        }
    }
    pub(crate) fn arguments(&self) -> Option<&str> {
        match self {
            Self::GameHook { arguments, .. } => Some(arguments),
            _ => None,
        }
    }
    pub(crate) fn entry_url(&self) -> Option<&str> {
        match self {
            Self::Webview { entry_url, .. } => Some(entry_url),
            _ => None,
        }
    }
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if self
            .video()
            .is_some_and(|video| !(128..=200_000).contains(&video.bitrate_kbps))
        {
            return Err(StoreError::InvalidInput);
        }
        match self {
            Self::GameHook {
                executable_path,
                arguments,
                ..
            } => {
                absolute_executable(executable_path)?;
                if arguments.len() > 8192
                    || arguments
                        .chars()
                        .any(|character| character.is_control() && character != '\t')
                {
                    return Err(StoreError::InvalidInput);
                }
            }
            Self::Webview { entry_url, .. } => webview_url(entry_url)?,
            Self::Rdp => {}
        }
        Ok(())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ApplicationSpec {
    pub name: String,
    pub access: ApplicationAccess,
    pub launch: ApplicationLaunch,
    pub disconnect_grace_seconds: u32,
    pub allow_observer: bool,
    pub allow_takeover: bool,
    pub disabled: bool,
}
impl ApplicationSpec {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if !(1..=128).contains(&self.name.chars().count())
            || self.name.trim() != self.name
            || self.name.chars().any(char::is_control)
        {
            return Err(StoreError::InvalidInput);
        }
        if !(1..=3600).contains(&self.disconnect_grace_seconds) {
            return Err(StoreError::InvalidInput);
        }
        self.launch.validate()?;
        if matches!(self.launch, ApplicationLaunch::Rdp)
            && (self.allow_observer || self.allow_takeover)
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}

fn absolute_executable(executable_path: &str) -> Result<(), StoreError> {
    if px_node_protocol::is_absolute_windows_executable_path(executable_path) {
        Ok(())
    } else {
        Err(StoreError::InvalidInput)
    }
}
fn webview_url(value: &str) -> Result<(), StoreError> {
    if value.is_empty()
        || value.len() > 8192
        || value.trim() != value
        || value.chars().any(char::is_control)
    {
        return Err(StoreError::InvalidInput);
    }
    let parsed = Url::parse(value).map_err(|_| StoreError::InvalidInput)?;
    if parsed.host().is_none() || !parsed.username().is_empty() || parsed.password().is_some() {
        return Err(StoreError::InvalidInput);
    }
    let internal = match parsed.host() {
        Some(Host::Ipv4(ip)) => ip.is_loopback() || ip.is_private() || ip.is_link_local(),
        Some(Host::Ipv6(ip)) => ip.is_loopback() || ip.is_unique_local(),
        Some(Host::Domain(host)) => {
            host.eq_ignore_ascii_case("localhost") || host.to_ascii_lowercase().ends_with(".local")
        }
        None => false,
    };
    if parsed.scheme() != "https" && !(parsed.scheme() == "http" && internal) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn disconnect_grace_is_bounded_for_every_application_kind() {
        for launch in [
            ApplicationLaunch::Rdp,
            ApplicationLaunch::Webview {
                entry_url: "https://example.com".into(),
                video: VideoSpec {
                    codec: VideoCodec::H264,
                    bitrate_kbps: 8000,
                },
            },
        ] {
            let mut spec = ApplicationSpec {
                name: "Grace test".into(),
                access: ApplicationAccess::Public,
                launch,
                disconnect_grace_seconds: 10,
                allow_observer: false,
                allow_takeover: false,
                disabled: false,
            };
            for seconds in [1, 10, 30, 3600] {
                spec.disconnect_grace_seconds = seconds;
                assert!(spec.validate().is_ok());
            }
            for seconds in [0, 3601, u32::MAX] {
                spec.disconnect_grace_seconds = seconds;
                assert!(spec.validate().is_err());
            }
        }
    }

    #[test]
    fn executable_paths_preserve_unicode_spaces_but_reject_escape_and_windows_aliases() {
        for good in [
            r"C:\game.exe",
            r"D:\游戏\版本 1\启动 器.exe",
            r"E:\a.b\app.EXE",
        ] {
            assert!(absolute_executable(good).is_ok());
        }
        for bad in [
            "",
            "app.exe",
            "\\app.exe",
            "..\\app.exe",
            "a\\..\\app.exe",
            "a\\.\\app.exe",
            "a//b.exe",
            "a\\\\b.exe",
            "a.\\b.exe",
            "a \\b.exe",
            "NUL.exe",
            "COM1\\b.exe",
            "LPT².exe",
            "a.exe:other.exe",
            "a\n.exe",
            "a.dll",
            "https://a.exe",
        ] {
            assert!(absolute_executable(bad).is_err(), "{bad:?}");
        }
    }
    #[test]
    fn webview_urls_keep_exact_value_and_reject_public_http_and_credentials() {
        for good in [
            "https://example.test/path?q=1#spa",
            "http://localhost:8080/",
            "http://127.0.0.1/",
            "http://192.168.52.2/",
            "http://[::1]/",
            "http://apps.local/",
        ] {
            assert!(webview_url(good).is_ok());
        }
        for bad in [
            " https://example.test/",
            "http://example.test/",
            "https://u:p@example.test/",
            "file:///C:/app.exe",
            "javascript:alert(1)",
            "https://example.test/\n",
            "http://[::]/",
        ] {
            assert!(webview_url(bad).is_err(), "{bad:?}");
        }
    }
    #[test]
    fn modes_reject_rdp_observers_and_media_bounds_without_modifying_arguments() {
        let mut spec = ApplicationSpec {
            name: "应用".into(),
            access: ApplicationAccess::Acl,
            launch: ApplicationLaunch::Rdp,
            disconnect_grace_seconds: 10,
            allow_observer: false,
            allow_takeover: false,
            disabled: false,
        };
        assert!(spec.validate().is_ok());
        spec.allow_observer = true;
        assert!(spec.validate().is_err());
        let args = "--name \"甲 乙\" --path \"C:\\目录\\文件\"";
        spec.launch = ApplicationLaunch::GameHook {
            executable_path: r"C:\app.exe".into(),
            arguments: args.into(),
            video: VideoSpec {
                codec: VideoCodec::H265,
                bitrate_kbps: 20_000,
            },
        };
        assert!(spec.validate().is_ok());
        assert_eq!(spec.launch.arguments(), Some(args));
        if let ApplicationLaunch::GameHook { video, .. } = &mut spec.launch {
            video.bitrate_kbps = u32::MAX;
        }
        assert!(spec.validate().is_err());
    }
}
