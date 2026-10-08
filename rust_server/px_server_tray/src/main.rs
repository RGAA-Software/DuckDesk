#![cfg_attr(windows, windows_subsystem = "windows")]

#[cfg(not(windows))]
fn main() {
    eprintln!("Pixels Server tray is supported only on Windows.");
}

#[cfg(windows)]
mod windows_tray {
    use std::ffi::c_void;
    use std::mem::size_of;
    use std::os::windows::ffi::OsStrExt;
    use std::path::PathBuf;
    use std::ptr::{null, null_mut};
    use windows_sys::Win32::Foundation::{HWND, POINT, RECT};
    use windows_sys::Win32::Globalization::GetUserDefaultUILanguage;
    use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
    use windows_sys::Win32::System::Registry::{
        RegGetValueW, HKEY_LOCAL_MACHINE, RRF_RT_REG_SZ, RRF_SUBKEY_WOW6432KEY,
        RRF_SUBKEY_WOW6464KEY,
    };
    use windows_sys::Win32::UI::Shell::{
        ShellExecuteW, Shell_NotifyIconW, NIF_ICON, NIF_MESSAGE, NIF_TIP, NIM_ADD, NIM_DELETE,
        NOTIFYICONDATAW,
    };
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        AppendMenuW, CreatePopupMenu, CreateWindowExW, DefWindowProcW, DestroyIcon, DestroyMenu,
        DestroyWindow, DispatchMessageW, FindWindowW, GetCursorPos, GetMessageW, LoadImageW,
        MessageBoxW, PostQuitMessage, RegisterClassW, SetForegroundWindow, TrackPopupMenu,
        TranslateMessage, IMAGE_ICON, LR_LOADFROMFILE, MB_ICONERROR, MB_OK, MF_SEPARATOR,
        MF_STRING, MSG, SW_SHOWNORMAL, TPM_RIGHTBUTTON, WM_APP, WM_COMMAND, WM_DESTROY,
        WM_LBUTTONDBLCLK, WM_RBUTTONUP, WNDCLASSW, WS_OVERLAPPED,
    };

    const TRAY_MESSAGE: u32 = WM_APP + 1;
    const OPEN_COMMAND: usize = 1;
    const EXIT_COMMAND: usize = 2;

    fn wide(value: &str) -> Vec<u16> {
        std::ffi::OsStr::new(value)
            .encode_wide()
            .chain(Some(0))
            .collect()
    }

    fn is_chinese() -> bool {
        // The primary language identifier for Chinese is 0x04.
        unsafe { GetUserDefaultUILanguage() & 0x03ff == 0x04 }
    }

    fn show_error(message: &str) {
        let message = wide(message);
        let title = wide("Pixels Server");
        unsafe {
            MessageBoxW(
                null_mut(),
                message.as_ptr(),
                title.as_ptr(),
                MB_OK | MB_ICONERROR,
            )
        };
    }

    fn console_url() -> Result<String, &'static str> {
        let key = wide(r"Software\Pixels\SingleServer");
        let field = wide("ConsoleOrigin");
        let mut buffer = [0_u16; 1024];
        let found = [RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY]
            .into_iter()
            .any(|registry_view| {
                let mut byte_count =
                    u32::try_from(size_of_val(&buffer)).expect("registry buffer size fits u32");
                unsafe {
                    RegGetValueW(
                        HKEY_LOCAL_MACHINE,
                        key.as_ptr(),
                        field.as_ptr(),
                        RRF_RT_REG_SZ | registry_view,
                        null_mut(),
                        buffer.as_mut_ptr().cast::<c_void>(),
                        &mut byte_count,
                    ) == 0
                }
            });
        if !found {
            return Err("Console address is not configured.");
        }
        let length = buffer
            .iter()
            .position(|character| *character == 0)
            .unwrap_or(buffer.len());
        let address =
            String::from_utf16(&buffer[..length]).map_err(|_| "Console address is invalid.")?;
        let parsed = url::Url::parse(&address).map_err(|_| "Console address is invalid.")?;
        if parsed.scheme() != "https"
            || parsed.host_str().is_none()
            || !parsed.username().is_empty()
            || parsed.password().is_some()
        {
            return Err("Console address must be an HTTPS URL without credentials.");
        }
        Ok(parsed.to_string())
    }

    fn open_console() {
        let result = console_url().and_then(|address| {
            let address = wide(&address);
            let operation = wide("open");
            let shell_result = unsafe {
                ShellExecuteW(
                    null_mut(),
                    operation.as_ptr(),
                    address.as_ptr(),
                    null(),
                    null(),
                    SW_SHOWNORMAL,
                )
            };
            if (shell_result as usize) <= 32 {
                Err("Unable to open the Console in the default browser.")
            } else {
                Ok(())
            }
        });
        if let Err(message) = result {
            show_error(if is_chinese() {
                "无法打开 Pixels 管理网页，请检查服务器安装状态。"
            } else {
                message
            });
        }
    }

    unsafe fn show_menu(window: HWND) {
        let menu = CreatePopupMenu();
        if menu.is_null() {
            return;
        }
        let open_label = wide(if is_chinese() {
            "打开管理网页"
        } else {
            "Open Console"
        });
        let exit_label = wide(if is_chinese() {
            "退出托盘"
        } else {
            "Exit Tray"
        });
        AppendMenuW(menu, MF_STRING, OPEN_COMMAND, open_label.as_ptr());
        AppendMenuW(menu, MF_SEPARATOR, 0, null());
        AppendMenuW(menu, MF_STRING, EXIT_COMMAND, exit_label.as_ptr());
        let mut cursor = POINT::default();
        if GetCursorPos(&mut cursor) != 0 {
            SetForegroundWindow(window);
            TrackPopupMenu(
                menu,
                TPM_RIGHTBUTTON,
                cursor.x,
                cursor.y,
                0,
                window,
                null::<RECT>(),
            );
        }
        DestroyMenu(menu);
    }

    unsafe extern "system" fn window_proc(
        window: HWND,
        message: u32,
        command: usize,
        event: isize,
    ) -> isize {
        match message {
            TRAY_MESSAGE if event as u32 == WM_LBUTTONDBLCLK => open_console(),
            TRAY_MESSAGE if event as u32 == WM_RBUTTONUP => show_menu(window),
            WM_COMMAND if command & 0xffff == OPEN_COMMAND => open_console(),
            WM_COMMAND if command & 0xffff == EXIT_COMMAND => {
                DestroyWindow(window);
            }
            WM_DESTROY => PostQuitMessage(0),
            _ => return DefWindowProcW(window, message, command, event),
        }
        0
    }

    fn icon_path() -> Option<PathBuf> {
        let executable = std::env::current_exe().ok()?;
        Some(
            executable
                .parent()?
                .parent()?
                .join("assets")
                .join("tray.ico"),
        )
    }

    pub fn run() -> Result<(), &'static str> {
        let class_name = wide("PixelsServerTrayWindow");
        unsafe {
            if !FindWindowW(class_name.as_ptr(), null()).is_null() {
                return Ok(());
            }
            let module = GetModuleHandleW(null());
            if module.is_null() {
                return Err("Unable to initialize tray module.");
            }
            let window_class = WNDCLASSW {
                lpfnWndProc: Some(window_proc),
                hInstance: module,
                lpszClassName: class_name.as_ptr(),
                ..Default::default()
            };
            if RegisterClassW(&window_class) == 0 {
                return Err("Unable to register tray window.");
            }
            let window = CreateWindowExW(
                0,
                class_name.as_ptr(),
                class_name.as_ptr(),
                WS_OVERLAPPED,
                0,
                0,
                0,
                0,
                null_mut(),
                null_mut(),
                module,
                null(),
            );
            if window.is_null() {
                return Err("Unable to create tray window.");
            }
            let icon_file = icon_path().ok_or("Tray icon path is unavailable.")?;
            let icon_file = wide(&icon_file.to_string_lossy());
            let icon = LoadImageW(
                null_mut(),
                icon_file.as_ptr(),
                IMAGE_ICON,
                0,
                0,
                LR_LOADFROMFILE,
            );
            if icon.is_null() {
                DestroyWindow(window);
                return Err("Tray icon is unavailable.");
            }
            let mut notification: NOTIFYICONDATAW = std::mem::zeroed();
            notification.cbSize =
                u32::try_from(size_of::<NOTIFYICONDATAW>()).expect("icon structure size fits u32");
            notification.hWnd = window;
            notification.uID = 1;
            notification.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            notification.uCallbackMessage = TRAY_MESSAGE;
            notification.hIcon = icon;
            let tooltip = wide("Pixels Server");
            notification.szTip[..tooltip.len()].copy_from_slice(&tooltip);
            if Shell_NotifyIconW(NIM_ADD, &notification) == 0 {
                DestroyIcon(icon);
                DestroyWindow(window);
                return Err("Unable to create the tray icon.");
            }
            let mut message = MSG::default();
            while GetMessageW(&mut message, null_mut(), 0, 0) > 0 {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Shell_NotifyIconW(NIM_DELETE, &notification);
            DestroyIcon(icon);
        }
        Ok(())
    }

    pub fn report_error(message: &str) {
        show_error(message);
    }
}

#[cfg(windows)]
fn main() {
    if let Err(message) = windows_tray::run() {
        windows_tray::report_error(message);
    }
}
