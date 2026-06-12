use std::ffi::OsStr;
use std::iter;
use std::mem::size_of;
use std::os::windows::ffi::OsStrExt;

use windows::core::PCWSTR;
use windows::Win32::Foundation::{COLORREF, GetLastError, HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::HBRUSH;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, DestroyWindow, RegisterClassExW, SetLayeredWindowAttributes,
    SetWindowPos, ShowWindow, HWND_TOPMOST, LWA_ALPHA, SW_SHOWNOACTIVATE, WINDOW_EX_STYLE, WINDOW_STYLE,
    WNDCLASSEXW, WS_EX_LAYERED, WS_EX_TOOLWINDOW, WS_EX_TRANSPARENT, WS_POPUP,
    WS_VISIBLE, CW_USEDEFAULT, SET_WINDOW_POS_FLAGS, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE,
};

const WINDOW_CLASS_NAME: &str = "GammaRayGuardHiddenWindow";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HiddenWindowSpec {
    pub width: i32,
    pub height: i32,
    pub ex_style: u32,
    pub style: u32,
    pub alpha: u8,
    pub topmost_flags: u32,
}

impl Default for HiddenWindowSpec {
    fn default() -> Self {
        Self {
            width: 10,
            height: 10,
            ex_style: (WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT).0,
            style: (WS_POPUP | WS_VISIBLE).0,
            alpha: 0,
            topmost_flags: (SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE).0,
        }
    }
}

pub struct HiddenWindow {
    hwnd: HWND,
}

impl HiddenWindow {
    pub fn create() -> Result<Self, String> {
        let instance = unsafe { GetModuleHandleW(None) }.map_err(|err| err.to_string())?;
        let class_name = wide(WINDOW_CLASS_NAME);
        let wc = WNDCLASSEXW {
            cbSize: size_of::<WNDCLASSEXW>() as u32,
            lpfnWndProc: Some(window_proc),
            hInstance: instance.into(),
            lpszClassName: PCWSTR(class_name.as_ptr()),
            hbrBackground: HBRUSH::default(),
            ..Default::default()
        };
        let atom = unsafe { RegisterClassExW(&wc) };
        if atom == 0 && unsafe { GetLastError().0 } != 1410 {
            return Err(format!("RegisterClassExW failed: {}", unsafe { GetLastError().0 }));
        }

        let spec = HiddenWindowSpec::default();
        let hwnd = unsafe {
            CreateWindowExW(
                WINDOW_EX_STYLE(spec.ex_style),
                PCWSTR(class_name.as_ptr()),
                PCWSTR(class_name.as_ptr()),
                WINDOW_STYLE(spec.style),
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                spec.width,
                spec.height,
                None,
                None,
                Some(instance.into()),
                None,
            )
        }
        .map_err(|err| format!("CreateWindowExW failed: {err}"))?;

        unsafe {
            SetLayeredWindowAttributes(hwnd, COLORREF(0), spec.alpha, LWA_ALPHA)
                .map_err(|err| err.to_string())?;
            SetWindowPos(
                hwnd,
                Some(HWND_TOPMOST),
                0,
                0,
                0,
                0,
                SET_WINDOW_POS_FLAGS(spec.topmost_flags),
            )
            .map_err(|err| err.to_string())?;
            let _ = ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }

        Ok(Self { hwnd })
    }

    pub fn hwnd(&self) -> HWND {
        self.hwnd
    }
}

impl Drop for HiddenWindow {
    fn drop(&mut self) {
        if !self.hwnd.0.is_null() {
            unsafe {
                let _ = DestroyWindow(self.hwnd);
            }
        }
    }
}

unsafe extern "system" fn window_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    DefWindowProcW(hwnd, msg, wparam, lparam)
}

fn wide(value: &str) -> Vec<u16> {
    OsStr::new(value)
        .encode_wide()
        .chain(iter::once(0))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hidden_window_spec_matches_qt_window_shape() {
        let spec = HiddenWindowSpec::default();
        assert_eq!(spec.width, 10);
        assert_eq!(spec.height, 10);
        assert_ne!(spec.ex_style & WS_EX_TOOLWINDOW.0, 0);
        assert_ne!(spec.ex_style & WS_EX_LAYERED.0, 0);
        assert_ne!(spec.ex_style & WS_EX_TRANSPARENT.0, 0);
        assert_ne!(spec.style & WS_POPUP.0, 0);
        assert_ne!(spec.style & WS_VISIBLE.0, 0);
    }

    #[test]
    fn hidden_window_alpha_is_zero() {
        assert_eq!(HiddenWindowSpec::default().alpha, 0);
    }

    #[test]
    fn topmost_flags_keep_window_non_activating() {
        let spec = HiddenWindowSpec::default();
        assert_ne!(spec.topmost_flags & SWP_NOMOVE.0, 0);
        assert_ne!(spec.topmost_flags & SWP_NOSIZE.0, 0);
        assert_ne!(spec.topmost_flags & SWP_NOACTIVATE.0, 0);
    }

    #[test]
    fn hidden_window_can_be_created() {
        let window = HiddenWindow::create().expect("create hidden window");
        assert!(!window.hwnd().0.is_null());
    }
}
