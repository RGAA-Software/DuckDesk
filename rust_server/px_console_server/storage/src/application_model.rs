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
        executable_relative: String,
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
                executable_relative,
                ..
            } => Some(executable_relative),
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
                executable_relative,
                arguments,
                ..
            } => {
                relative_executable(executable_relative)?;
                if arguments.len() > 8192 || arguments.chars().any(|c| c.is_control() && c != '\t')
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
        self.launch.validate()?;
        if matches!(self.launch, ApplicationLaunch::Rdp)
            && (self.allow_observer || self.allow_takeover)
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}

fn relative_executable(value: &str) -> Result<(), StoreError> {
    if !value.to_ascii_lowercase().ends_with(".exe") {
        return Err(StoreError::InvalidInput);
    }
    windows_relative_components(value)
}
pub(crate) fn windows_relative_components(value: &str) -> Result<(), StoreError> {
    if value.is_empty()
        || value.len() > 2048
        || value
            .chars()
            .any(|c| c.is_control() || matches!(c, '/' | ':' | '"' | '<' | '>' | '|' | '?' | '*'))
    {
        return Err(StoreError::InvalidInput);
    }
    for component in value.split('\\') {
        let stem = component
            .split('.')
            .next()
            .unwrap_or_default()
            .to_ascii_uppercase();
        let reserved = matches!(
            stem.as_str(),
            "CON" | "PRN" | "AUX" | "NUL" | "CONIN$" | "CONOUT$"
        ) || ["COM", "LPT"].iter().any(|prefix| {
            stem.strip_prefix(prefix).is_some_and(|suffix| {
                matches!(
                    suffix,
                    "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "¹" | "²" | "³"
                )
            })
        });
        if component.is_empty()
            || component == "."
            || component == ".."
            || component.ends_with([' ', '.'])
            || reserved
        {
            return Err(StoreError::InvalidInput);
        }
    }
    Ok(())
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
    fn executable_paths_preserve_unicode_spaces_but_reject_escape_and_windows_aliases() {
        for good in ["game.exe", "游戏\\版本 1\\启动 器.exe", "a.b\\app.EXE"] {
            assert!(relative_executable(good).is_ok());
        }
        for bad in [
            "",
            "C:\\app.exe",
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
            assert!(relative_executable(bad).is_err(), "{bad:?}");
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
            allow_observer: false,
            allow_takeover: false,
            disabled: false,
        };
        assert!(spec.validate().is_ok());
        spec.allow_observer = true;
        assert!(spec.validate().is_err());
        let args = "--name \"甲 乙\" --path \"C:\\目录\\文件\"";
        spec.launch = ApplicationLaunch::GameHook {
            executable_relative: "app.exe".into(),
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
