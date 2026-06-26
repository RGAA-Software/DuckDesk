use std::fs;
use std::path::PathBuf;
use std::time::Duration;

use gpui::{
    App, AppContext, Context, IntoElement, ParentElement, Render, Styled, Window, WindowBounds,
    WindowOptions, div, px, size,
};
use gpui_component::{
    ActiveTheme, Root, Theme, ThemeMode, TitleBar,
    button::{Button, ButtonVariants as _},
    h_flex,
    input::{Input, InputState},
    scroll::ScrollableElement,
    tab::{Tab, TabBar},
    v_flex,
};
use gpui_component_assets::Assets;
use smol::Timer;
use serde::{Deserialize, Serialize};

use crate::monitor_dashboard::{render_connection_summary, render_dashboard};
use crate::monitor_model::{MachineTelemetry, MonitorTab};
use crate::monitor_sender::{MonitorSenderHandle, SenderStatus};
use crate::sys_info_mgr::SysInfoManager;
use crate::tray::{self, TrayCommand};

const INTERVAL: Duration = Duration::from_secs(1);
const DEFAULT_MONITOR_HOST: &str = "127.0.0.1";
const DEFAULT_MONITOR_PORT: u16 = 20379;

#[derive(Clone, Debug, Serialize, Deserialize)]
struct MonitorConnectionConfig {
    host: String,
    port: u16,
    auto_connect: bool,
}

impl Default for MonitorConnectionConfig {
    fn default() -> Self {
        Self {
            host: DEFAULT_MONITOR_HOST.to_string(),
            port: DEFAULT_MONITOR_PORT,
            auto_connect: true,
        }
    }
}

fn monitor_connection_config_path() -> PathBuf {
    let mut base = std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir);
    base.push("GammaRayPremium");
    base.push("GrSysMonitor");
    base.push("connection.json");
    base
}

fn load_monitor_connection_config() -> MonitorConnectionConfig {
    let path = monitor_connection_config_path();
    fs::read_to_string(path)
        .ok()
        .and_then(|content| serde_json::from_str::<MonitorConnectionConfig>(&content).ok())
        .unwrap_or_default()
}

fn save_monitor_connection_config(config: &MonitorConnectionConfig) {
    let path = monitor_connection_config_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Ok(content) = serde_json::to_string_pretty(config) {
        let _ = fs::write(path, content);
    }
}

pub struct SysMonitorApp {
    manager: SysInfoManager,
    telemetry: MachineTelemetry,
    active_tab: MonitorTab,
    host_input: gpui::Entity<InputState>,
    port_input: gpui::Entity<InputState>,
    sender: MonitorSenderHandle,
    sender_status: SenderStatus,
}

impl SysMonitorApp {
    fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
        let mut manager = SysInfoManager::new();
        let current = manager.load_system_info();
        let telemetry = MachineTelemetry::new(current.clone());
        let connection_config = load_monitor_connection_config();
        let sender = MonitorSenderHandle::new();
        sender.set_latest_json(serde_json::to_string(&current).unwrap_or_default());
        if connection_config.auto_connect {
            sender.connect(connection_config.host.clone(), connection_config.port);
        } else {
            sender.disconnect();
        }

        let host_input = cx.new(|cx| {
            InputState::new(window, cx)
                .default_value(&connection_config.host)
                .placeholder("host")
        });
        let port_input = cx.new(|cx| {
            InputState::new(window, cx)
                .default_value(connection_config.port.to_string())
                .placeholder("port")
        });

        let mut this = Self {
            manager,
            telemetry,
            active_tab: MonitorTab::Overview,
            host_input,
            port_input,
            sender,
            sender_status: SenderStatus::default(),
        };
        this.sender_status = this.sender.status();

        cx.spawn(async move |this, cx| {
            loop {
                Timer::after(INTERVAL).await;
                if this
                    .update(cx, |this, cx| {
                        this.refresh();
                        cx.notify();
                    })
                    .is_err()
                {
                    break;
                }
            }
        })
        .detach();

        this
    }

    fn refresh(&mut self) {
        let snapshot = self.manager.load_system_info();
        let json = serde_json::to_string(&snapshot).unwrap_or_default();
        self.telemetry.apply_snapshot(snapshot);
        self.sender.set_latest_json(json);
        self.sender_status = self.sender.status();
    }

    fn current_connection_config(
        &self,
        cx: &Context<Self>,
        auto_connect: bool,
    ) -> MonitorConnectionConfig {
        MonitorConnectionConfig {
            host: self.host_input.read(cx).value().to_string(),
            port: self
                .port_input
                .read(cx)
                .value()
                .parse::<u16>()
                .unwrap_or(DEFAULT_MONITOR_PORT),
            auto_connect,
        }
    }

    fn set_active_tab(&mut self, index: usize, _window: &mut Window, cx: &mut Context<Self>) {
        self.active_tab = MonitorTab::from_index(index);
        cx.notify();
    }

    fn connect_sender(&mut self, _event: &gpui::ClickEvent, _window: &mut Window, cx: &mut Context<Self>) {
        let config = self.current_connection_config(cx, true);
        self.sender.connect(config.host.clone(), config.port);
        save_monitor_connection_config(&config);
        self.sender_status = self.sender.status();
        cx.notify();
    }

    fn disconnect_sender(
        &mut self,
        _event: &gpui::ClickEvent,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        self.sender.disconnect();
        save_monitor_connection_config(&self.current_connection_config(cx, false));
        self.sender_status = self.sender.status();
        cx.notify();
    }

    fn render_connection_panel(&self, cx: &Context<Self>) -> impl IntoElement {
        v_flex()
            .gap_3()
            .px_4()
            .py_3()
            .bg(cx.theme().secondary)
            .border_b_1()
            .border_color(cx.theme().border)
            .child(
                h_flex()
                    .gap_3()
                    .items_center()
                    .child(div().text_sm().text_color(cx.theme().foreground).child("推送到 Host"))
                    .child(div().w(px(240.)).child(Input::new(&self.host_input)))
                    .child(div().w(px(120.)).child(Input::new(&self.port_input)))
                    .child(
                        Button::new("monitor-connect")
                            .primary()
                            .label("Connect / Reconnect")
                            .on_click(cx.listener(Self::connect_sender)),
                    )
                    .child(
                        Button::new("monitor-disconnect")
                            .outline()
                            .label("Disconnect")
                            .on_click(cx.listener(Self::disconnect_sender)),
                    ),
            )
            .child(render_connection_summary(
                &format!(
                    "状态: {} | 目标: {}{}",
                    self.sender_status.state,
                    self.sender_status.target,
                    if self.sender_status.last_error.is_empty() {
                        String::new()
                    } else {
                        format!(" | 错误: {}", self.sender_status.last_error)
                    }
                ),
                cx,
            ))
    }
}

impl Render for SysMonitorApp {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let active_tab_index = self.active_tab as usize;
        v_flex()
            .size_full()
            .bg(cx.theme().background)
            .child(
                TitleBar::new()
                    .child(
                        h_flex()
                            .gap_3()
                            .items_center()
                            .child(div().text_sm().text_color(cx.theme().foreground).child("GrSysMonitor"))
                            .child(
                                div()
                                    .text_xs()
                                    .text_color(cx.theme().muted_foreground)
                                    .child(format!(
                                        "{} | {} | {}",
                                        self.telemetry.current.os.sys_host_name,
                                        self.telemetry.current.os.sys_os_long_version,
                                        self.telemetry.current.uptime
                                    )),
                            ),
                    )
                    .child(
                        div()
                            .mr_4()
                            .text_xs()
                            .text_color(cx.theme().muted_foreground)
                            .child(format!("刷新时间 {}", self.telemetry.current.timestamp_readable)),
                    ),
            )
            .child(self.render_connection_panel(cx))
            .child(
                TabBar::new("monitor-tabs")
                    .segmented()
                    .px_3()
                    .py_2()
                    .selected_index(active_tab_index)
                    .on_click(cx.listener(|this, ix: &usize, window, cx| {
                        this.set_active_tab(*ix, window, cx);
                    }))
                    .child(Tab::new().label("总览"))
                    .child(Tab::new().label("CPU / 内存"))
                    .child(Tab::new().label("GPU"))
                    .child(Tab::new().label("存储"))
                    .child(Tab::new().label("网络"))
                    .child(Tab::new().label("传感器")),
            )
            .child(
                div()
                    .flex_1()
                    .overflow_y_scrollbar()
                    .child(render_dashboard(&self.telemetry, self.active_tab, cx)),
            )
    }
}

pub fn run(start_hidden: bool) {
    tray::init_tray("GrSysMonitor");
    let app = gpui_platform::application().with_assets(Assets);
    app.run(move |cx: &mut App| {
        gpui_component::init(cx);

        let window_options = WindowOptions {
            titlebar: Some(TitleBar::title_bar_options()),
            window_bounds: Some(WindowBounds::centered(size(px(1480.), px(980.)), cx)),
            ..Default::default()
        };

        cx.spawn(async move |cx| {
            let window_handle = cx.open_window(window_options, |window, cx| {
                window.set_window_title("GrSysMonitor");

                Theme::change(ThemeMode::Dark, Some(window), cx);

                window.on_window_should_close(cx, |window, _cx| {
                    tray::hide_window(window);
                    false
                });

                if start_hidden {
                    tray::hide_window(window);
                } else {
                    window.activate_window();
                }

                let view = cx.new(|cx| SysMonitorApp::new(window, cx));
                cx.new(|cx| Root::new(view, window, cx))
            })
            .expect("failed to open GrSysMonitor window");

            #[allow(clippy::redundant_locals)]
            cx.spawn({
                let window_handle = window_handle;
                async move |cx| loop {
                    Timer::after(Duration::from_millis(200)).await;
                    for command in tray::take_commands() {
                        match command {
                            TrayCommand::ShowWindow => {
                                let _ = window_handle.update(cx, |_, window, _| {
                                    tray::show_window(window);
                                    window.activate_window();
                                });
                            }
                            TrayCommand::ExitApp => {
                                let _ = window_handle.update(cx, |_, window, _| {
                                    window.remove_window();
                                });
                                cx.update(|cx| cx.quit());
                                return;
                            }
                        }
                    }
                }
            })
            .detach();
        })
        .detach();
    });
}
