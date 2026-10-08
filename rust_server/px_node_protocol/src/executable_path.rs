/// Validate a drive-qualified Windows executable path without changing its spelling.
/// Paths stay exact through scheduling; process admission also checks the private Job.
pub fn is_absolute_windows_executable_path(executable_path: &str) -> bool {
    let path_bytes = executable_path.as_bytes();
    if path_bytes.len() < 7
        || path_bytes.len() > 2048
        || !path_bytes[0].is_ascii_alphabetic()
        || &path_bytes[1..3] != b":\\"
        || !executable_path.to_ascii_lowercase().ends_with(".exe")
    {
        return false;
    }
    let path_tail = &executable_path[3..];
    if path_tail.chars().any(|character| {
        character.is_control() || matches!(character, '/' | ':' | '"' | '<' | '>' | '|' | '?' | '*')
    }) {
        return false;
    }
    path_tail.split('\\').all(|component| {
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
        !component.is_empty()
            && !matches!(component, "." | "..")
            && !component.ends_with([' ', '.'])
            && !reserved
    })
}

#[cfg(test)]
mod tests {
    use super::is_absolute_windows_executable_path;

    #[test]
    fn accepts_exact_local_executables_with_unicode_and_spaces() {
        for executable_path in [
            r"D:\software\2dadventure\2dAdventure.exe",
            r"C:\游戏 目录\启动 器.EXE",
            r"c:\app.exe",
        ] {
            assert!(
                is_absolute_windows_executable_path(executable_path),
                "{executable_path}"
            );
        }
    }

    #[test]
    fn rejects_relative_ambiguous_device_and_non_executable_paths() {
        for executable_path in [
            "",
            "game.exe",
            r"games\game.exe",
            r"C:game.exe",
            r"\game.exe",
            r"\\host\share\game.exe",
            r"\\?\C:\game.exe",
            r"C:\..\game.exe",
            r"C:\.\game.exe",
            r"C:\dir.\game.exe",
            r"C:\dir \game.exe",
            r"C:\NUL.exe",
            r"C:\COM¹\game.exe",
            r"C:\game.exe:stream.exe",
            r"C:/game.exe",
            r"C:\game.dll",
            "C:\\ga\nme.exe",
            "\"C:\\game.exe\"",
            r"C:\game.exe --argument",
        ] {
            assert!(
                !is_absolute_windows_executable_path(executable_path),
                "{executable_path}"
            );
        }
    }
}
