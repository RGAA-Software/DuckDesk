use std::collections::VecDeque;
use std::sync::{Arc, Mutex, OnceLock};

use gpui::Window;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrayCommand {
    ShowWindow,
    ExitApp,
}

static COMMAND_QUEUE: OnceLock<Arc<Mutex<VecDeque<TrayCommand>>>> = OnceLock::new();

fn command_queue() -> Arc<Mutex<VecDeque<TrayCommand>>> {
    COMMAND_QUEUE
        .get_or_init(|| Arc::new(Mutex::new(VecDeque::new())))
        .clone()
}

fn push_command(command: TrayCommand) {
    if let Ok(mut queue) = command_queue().lock() {
        queue.push_back(command);
    }
}

pub fn take_commands() -> Vec<TrayCommand> {
    if let Ok(mut queue) = command_queue().lock() {
        return queue.drain(..).collect();
    }
    Vec::new()
}

#[cfg(target_os = "windows")]
mod windows_impl {
    use std::ffi::c_void;
    use std::mem::size_of;
    use std::ptr::null;
    use std::thread;

    use gpui::Window;
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};
    use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, POINT, WPARAM};
    use windows::Win32::System::LibraryLoader::GetModuleHandleW;
    use windows::Win32::UI::Shell::{
        NIF_ICON, NIF_MESSAGE, NIF_TIP, NIM_ADD, NIM_DELETE, NOTIFYICONDATAW, Shell_NotifyIconW,
    };
    use windows::Win32::UI::WindowsAndMessaging::{
        AppendMenuW, CreatePopupMenu, CreateWindowExW, DefWindowProcW, DestroyMenu, DispatchMessageW,
        GetCursorPos, GetMessageW, HICON, HMENU, IDI_APPLICATION, LoadIconW, MF_STRING, MSG,
        PostQuitMessage, RegisterClassW, SW_HIDE, SW_RESTORE, SW_SHOW, SetForegroundWindow,
        ShowWindow, TPM_BOTTOMALIGN, TPM_LEFTALIGN, TPM_RIGHTBUTTON, TrackPopupMenu, TranslateMessage,
        WINDOW_EX_STYLE, WINDOW_STYLE, WM_APP, WM_COMMAND, WM_CONTEXTMENU, WM_DESTROY, WM_LBUTTONDBLCLK,
        WM_LBUTTONUP, WM_RBUTTONUP, WNDCLASSW, WS_OVERLAPPEDWINDOW,
    };
    use windows::core::PCWSTR;

    use super::{TrayCommand, push_command};

    const TRAY_CALLBACK_MESSAGE: u32 = WM_APP + 1;
    const MENU_SHOW_ID: usize = 1001;
    const MENU_EXIT_ID: usize = 1002;

    pub fn init_tray(app_name: &str) {
        let tooltip = app_name.to_string();
        thread::spawn(move || unsafe {
            let hinstance = match GetModuleHandleW(None) {
                Ok(module) => module,
                Err(_) => return,
            };

            let class_name = to_wide("GrSysMonitorTrayWindow");
            let window_name = to_wide(&tooltip);

            let wnd_class = WNDCLASSW {
                lpfnWndProc: Some(tray_window_proc),
                hInstance: hinstance.into(),
                lpszClassName: PCWSTR(class_name.as_ptr()),
                ..Default::default()
            };
            let _ = RegisterClassW(&wnd_class);

            let hwnd = match CreateWindowExW(
                WINDOW_EX_STYLE::default(),
                PCWSTR(class_name.as_ptr()),
                PCWSTR(window_name.as_ptr()),
                WINDOW_STYLE(WS_OVERLAPPEDWINDOW.0),
                0,
                0,
                0,
                0,
                None,
                None,
                Some(hinstance.into()),
                None,
            ) {
                Ok(hwnd) => hwnd,
                Err(_) => return,
            };

            if !add_tray_icon(hwnd, &tooltip) {
                return;
            }

            let mut message = MSG::default();
            while GetMessageW(&mut message, None, 0, 0).into() {
                let _ = TranslateMessage(&message);
                let _ = DispatchMessageW(&message);
            }

            remove_tray_icon(hwnd);
        });
    }

    pub fn hide_window(window: &Window) {
        if let Some(hwnd) = hwnd_from_window(window) {
            unsafe {
                let _ = ShowWindow(hwnd, SW_HIDE);
            }
        }
    }

    pub fn show_window(window: &Window) {
        if let Some(hwnd) = hwnd_from_window(window) {
            unsafe {
                let _ = ShowWindow(hwnd, SW_RESTORE);
                let _ = ShowWindow(hwnd, SW_SHOW);
                let _ = SetForegroundWindow(hwnd);
            }
        }
    }

    fn hwnd_from_window(window: &Window) -> Option<HWND> {
        let handle = HasWindowHandle::window_handle(window).ok()?;
        match handle.as_raw() {
            RawWindowHandle::Win32(handle) => Some(HWND(handle.hwnd.get() as *mut c_void)),
            _ => None,
        }
    }

    unsafe fn add_tray_icon(hwnd: HWND, tooltip: &str) -> bool {
        let mut data = NOTIFYICONDATAW {
            cbSize: size_of::<NOTIFYICONDATAW>() as u32,
            hWnd: hwnd,
            uID: 1,
            uFlags: NIF_MESSAGE | NIF_ICON | NIF_TIP,
            uCallbackMessage: TRAY_CALLBACK_MESSAGE,
            hIcon: load_app_icon(),
            ..Default::default()
        };
        copy_tooltip(&mut data, tooltip);
        Shell_NotifyIconW(NIM_ADD, &data).as_bool()
    }

    unsafe fn remove_tray_icon(hwnd: HWND) {
        let data = NOTIFYICONDATAW {
            cbSize: size_of::<NOTIFYICONDATAW>() as u32,
            hWnd: hwnd,
            uID: 1,
            ..Default::default()
        };
        let _ = Shell_NotifyIconW(NIM_DELETE, &data);
    }

    unsafe fn load_app_icon() -> HICON {
        LoadIconW(None, IDI_APPLICATION).unwrap_or_default()
    }

    fn copy_tooltip(data: &mut NOTIFYICONDATAW, tooltip: &str) {
        let wide = to_wide(tooltip);
        for (index, value) in wide.iter().take(data.szTip.len().saturating_sub(1)).enumerate() {
            data.szTip[index] = *value;
        }
    }

    fn to_wide(value: &str) -> Vec<u16> {
        value.encode_utf16().chain(std::iter::once(0)).collect()
    }

    unsafe fn show_context_menu(hwnd: HWND) {
        let menu = CreatePopupMenu().unwrap_or(HMENU(null::<c_void>() as _));
        if menu.0.is_null() {
            return;
        }

        let show_text = to_wide("显示窗口");
        let exit_text = to_wide("退出");
        let _ = AppendMenuW(menu, MF_STRING, MENU_SHOW_ID, PCWSTR(show_text.as_ptr()));
        let _ = AppendMenuW(menu, MF_STRING, MENU_EXIT_ID, PCWSTR(exit_text.as_ptr()));

        let mut point = POINT::default();
        let _ = GetCursorPos(&mut point);
        let _ = SetForegroundWindow(hwnd);
        let _ = TrackPopupMenu(
            menu,
            TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
            point.x,
            point.y,
            Some(0),
            hwnd,
            None,
        );
        let _ = DestroyMenu(menu);
    }

    unsafe extern "system" fn tray_window_proc(
        hwnd: HWND,
        message: u32,
        wparam: WPARAM,
        lparam: LPARAM,
    ) -> LRESULT {
        match message {
            TRAY_CALLBACK_MESSAGE => match lparam.0 as u32 {
                WM_LBUTTONUP | WM_LBUTTONDBLCLK => {
                    push_command(TrayCommand::ShowWindow);
                    LRESULT(0)
                }
                WM_RBUTTONUP | WM_CONTEXTMENU => {
                    show_context_menu(hwnd);
                    LRESULT(0)
                }
                _ => DefWindowProcW(hwnd, message, wparam, lparam),
            },
            WM_COMMAND => {
                match wparam.0 & 0xffff {
                    MENU_SHOW_ID => push_command(TrayCommand::ShowWindow),
                    MENU_EXIT_ID => push_command(TrayCommand::ExitApp),
                    _ => {}
                }
                LRESULT(0)
            }
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            _ => DefWindowProcW(hwnd, message, wparam, lparam),
        }
    }
}

#[cfg(not(target_os = "windows"))]
mod windows_impl {
    use gpui::Window;

    pub fn init_tray(_app_name: &str) {}
    pub fn hide_window(_window: &Window) {}
    pub fn show_window(_window: &Window) {}
}

pub fn init_tray(app_name: &str) {
    windows_impl::init_tray(app_name);
}

pub fn hide_window(window: &Window) {
    windows_impl::hide_window(window);
}

pub fn show_window(window: &Window) {
    windows_impl::show_window(window);
}
