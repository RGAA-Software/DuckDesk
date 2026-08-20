use std::collections::BTreeMap;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use clap::Parser;
use futures_util::StreamExt;
use gpui::{
    div, prelude::FluentBuilder as _, px, size, App, AppContext, Context, IntoElement,
    ParentElement, Render, SharedString, Styled, Window, WindowBounds, WindowOptions,
};
use gpui_component::{
    button::{Button, ButtonVariants as _},
    h_flex,
    scroll::ScrollableElement,
    tab::{Tab, TabBar},
    v_flex, ActiveTheme, Icon, IconName, Root, Selectable, StyledExt, Theme, ThemeMode, TitleBar,
};
use gpui_component_assets::Assets;
use px_base::sys_info::SysInfo;
use smol::Timer;
use tokio::net::{TcpListener, TcpStream};
use tokio::runtime::Runtime;
use tokio_tungstenite::accept_hdr_async;
use tokio_tungstenite::tungstenite::handshake::server::{Request, Response};
use tokio_tungstenite::tungstenite::Message;

use crate::monitor_dashboard::{render_connection_summary, render_dashboard};
use crate::monitor_model::{MachineTelemetry, MonitorTab, RemoteMachineState};
use crate::tray::{self, TrayCommand};

const PATH_SYS_INFO: &str = "/sys/info";
const POLL_INTERVAL: Duration = Duration::from_secs(1);

#[derive(Parser, Clone)]
#[command(name = "PxSysMonitorHost", version, about)]
pub struct HostCli {
    #[arg(long, default_value = "0.0.0.0")]
    host: String,

    #[arg(long, default_value_t = 20379)]
    port: u16,

    #[arg(long, default_value_t = false)]
    pub startup: bool,
}

#[derive(Clone, Default)]
struct HostServerStatus {
    listen_addr: String,
    state: String,
    last_error: String,
}

#[derive(Clone)]
struct HostServerHandle {
    machines: Arc<Mutex<BTreeMap<String, RemoteMachineState>>>,
    status: Arc<Mutex<HostServerStatus>>,
}

impl HostServerHandle {
    fn new(listen_host: String, listen_port: u16) -> Self {
        let machines = Arc::new(Mutex::new(BTreeMap::new()));
        let status = Arc::new(Mutex::new(HostServerStatus {
            listen_addr: format!("{listen_host}:{listen_port}"),
            state: "Starting".to_string(),
            last_error: String::new(),
        }));

        spawn_host_server(listen_host, listen_port, machines.clone(), status.clone());

        Self { machines, status }
    }

    fn machines(&self) -> Vec<RemoteMachineState> {
        self.machines
            .lock()
            .map(|items| items.values().cloned().collect())
            .unwrap_or_default()
    }

    fn status(&self) -> HostServerStatus {
        self.status
            .lock()
            .map(|status| status.clone())
            .unwrap_or_default()
    }
}

fn spawn_host_server(
    listen_host: String,
    listen_port: u16,
    machines: Arc<Mutex<BTreeMap<String, RemoteMachineState>>>,
    status: Arc<Mutex<HostServerStatus>>,
) {
    thread::spawn(move || {
        let runtime = Runtime::new().expect("failed to create PxSysMonitorHost runtime");
        runtime.block_on(async move {
            let listen_addr = format!("{listen_host}:{listen_port}");
            match TcpListener::bind(&listen_addr).await {
                Ok(listener) => {
                    update_host_status(&status, &listen_addr, "Listening", "");
                    loop {
                        match listener.accept().await {
                            Ok((stream, _)) => {
                                let machines = machines.clone();
                                let status = status.clone();
                                tokio::spawn(async move {
                                    let _ = handle_host_client(stream, machines, status).await;
                                });
                            }
                            Err(err) => {
                                update_host_status(
                                    &status,
                                    &listen_addr,
                                    "AcceptFailed",
                                    &err.to_string(),
                                );
                            }
                        }
                    }
                }
                Err(err) => {
                    update_host_status(&status, &listen_addr, "BindFailed", &err.to_string());
                }
            }
        });
    });
}

async fn handle_host_client(
    stream: TcpStream,
    machines: Arc<Mutex<BTreeMap<String, RemoteMachineState>>>,
    status: Arc<Mutex<HostServerStatus>>,
) -> Result<(), String> {
    let peer_addr = stream
        .peer_addr()
        .map(|addr| addr.to_string())
        .unwrap_or_else(|_| "<unknown>".to_string());
    let peer_ip = stream
        .peer_addr()
        .map(|addr| addr.ip().to_string())
        .unwrap_or_else(|_| "<unknown>".to_string());

    #[allow(clippy::result_large_err)]
    let ws_stream = accept_hdr_async(stream, move |req: &Request, resp: Response| {
        if req.uri().path() == PATH_SYS_INFO {
            Ok(resp)
        } else {
            Err(
                tokio_tungstenite::tungstenite::handshake::server::ErrorResponse::new(Some(
                    "invalid path".to_string(),
                )),
            )
        }
    })
    .await
    .map_err(|err| err.to_string())?;

    let (_sink, mut source) = ws_stream.split();
    let mut machine_key = None::<String>;

    while let Some(message) = source.next().await {
        let message = message.map_err(|err| err.to_string())?;
        let Message::Text(text) = message else {
            continue;
        };

        match serde_json::from_str::<SysInfo>(&text) {
            Ok(info) => {
                let host_name = if info.os.sys_host_name.is_empty() {
                    "UnknownHost".to_string()
                } else {
                    info.os.sys_host_name.clone()
                };
                let key = peer_ip.clone();
                machine_key = Some(key.clone());

                if let Ok(mut items) = machines.lock() {
                    match items.get_mut(&key) {
                        Some(record) => {
                            record.display_name = host_name.clone();
                            record.peer_addr = peer_addr.clone();
                            record.peer_ip = peer_ip.clone();
                            record.active_peer_addr = peer_addr.clone();
                            record.last_seen = info.timestamp_readable.clone();
                            record.connected = true;
                            record.telemetry.apply_snapshot(info);
                        }
                        None => {
                            items.insert(
                                key.clone(),
                                RemoteMachineState {
                                    machine_id: key.clone(),
                                    display_name: host_name,
                                    peer_ip: peer_ip.clone(),
                                    peer_addr: peer_addr.clone(),
                                    active_peer_addr: peer_addr.clone(),
                                    last_seen: info.timestamp_readable.clone(),
                                    connected: true,
                                    telemetry: MachineTelemetry::new(info),
                                },
                            );
                        }
                    }
                }
            }
            Err(err) => {
                let snapshot = status.lock().map(|item| item.clone()).unwrap_or_default();
                update_host_status(
                    &status,
                    &snapshot.listen_addr,
                    "ParseFailed",
                    &err.to_string(),
                );
            }
        }
    }

    if let Some(machine_key) = machine_key {
        if let Ok(mut items) = machines.lock() {
            if let Some(record) = items.get_mut(&machine_key) {
                if record.active_peer_addr == peer_addr {
                    record.connected = false;
                }
            }
        }
    }

    Ok(())
}

fn update_host_status(
    status: &Arc<Mutex<HostServerStatus>>,
    listen_addr: &str,
    state: &str,
    last_error: &str,
) {
    if let Ok(mut current) = status.lock() {
        current.listen_addr = listen_addr.to_string();
        current.state = state.to_string();
        current.last_error = last_error.to_string();
    }
}

pub struct SysMonitorHostApp {
    server: HostServerHandle,
    machines: Vec<RemoteMachineState>,
    selected_machine: Option<String>,
    server_status: HostServerStatus,
    active_tab: MonitorTab,
}

impl SysMonitorHostApp {
    fn new(_window: &mut Window, cx: &mut Context<Self>, cli: HostCli) -> Self {
        let server = HostServerHandle::new(cli.host, cli.port);
        let mut this = Self {
            server,
            machines: Vec::new(),
            selected_machine: None,
            server_status: HostServerStatus::default(),
            active_tab: MonitorTab::Overview,
        };
        this.refresh();

        cx.spawn(async move |this, cx| loop {
            Timer::after(POLL_INTERVAL).await;
            if this
                .update(cx, |this, cx| {
                    this.refresh();
                    cx.notify();
                })
                .is_err()
            {
                break;
            }
        })
        .detach();

        this
    }

    fn refresh(&mut self) {
        self.server_status = self.server.status();
        self.machines = self.server.machines();
        self.machines
            .sort_by(|a, b| a.display_name.cmp(&b.display_name));

        let selected_exists = self.selected_machine.as_ref().is_some_and(|selected| {
            self.machines
                .iter()
                .any(|item| &item.machine_id == selected)
        });
        if !selected_exists {
            self.selected_machine = self.machines.first().map(|item| item.machine_id.clone());
        }
    }

    fn selected_machine(&self) -> Option<&RemoteMachineState> {
        self.selected_machine.as_ref().and_then(|selected| {
            self.machines
                .iter()
                .find(|item| &item.machine_id == selected)
        })
    }

    fn select_machine(&mut self, machine_id: &str, _window: &mut Window, cx: &mut Context<Self>) {
        self.selected_machine = Some(machine_id.to_owned());
        cx.notify();
    }

    fn set_active_tab(&mut self, index: usize, _window: &mut Window, cx: &mut Context<Self>) {
        self.active_tab = MonitorTab::from_index(index);
        cx.notify();
    }

    fn render_sidebar(&self, cx: &Context<Self>) -> impl IntoElement {
        v_flex()
            .w(px(280.))
            .h_full()
            .gap_2()
            .p_3()
            .border_r_1()
            .border_color(cx.theme().border)
            .bg(cx.theme().secondary)
            .child(
                div()
                    .text_sm()
                    .text_color(cx.theme().foreground)
                    .child("在线机器"),
            )
            .child(
                div()
                    .flex_1()
                    .overflow_y_scrollbar()
                    .child(
                        v_flex()
                            .gap_2()
                            .children(self.machines.iter().map(|machine| {
                                let selected = self
                                    .selected_machine
                                    .as_ref()
                                    .is_some_and(|item| item == &machine.machine_id);
                                let machine_id = machine.machine_id.clone();
                                Button::new(SharedString::from(format!(
                                    "machine-{}",
                                    machine.machine_id
                                )))
                                .ghost()
                                .selected(selected)
                                .w_full()
                                .h(px(50.))
                                .child(
                                    h_flex()
                                        .w_full()
                                        .h_full()
                                        .justify_between()
                                        .items_center()
                                        .child(
                                            v_flex()
                                                .justify_center()
                                                .items_start()
                                                .gap(px(3.))
                                                .child(
                                                    div()
                                                        .text_sm()
                                                        .font_semibold()
                                                        .text_color(cx.theme().foreground)
                                                        .child(machine.peer_ip.clone()),
                                                )
                                                .child(
                                                    h_flex()
                                                        .gap_2()
                                                        .items_center()
                                                        .child(
                                                            div()
                                                                .text_xs()
                                                                .text_color(if machine.connected {
                                                                    cx.theme().green
                                                                } else {
                                                                    cx.theme().red
                                                                })
                                                                .child(if machine.connected {
                                                                    "在线".to_string()
                                                                } else {
                                                                    "离线".to_string()
                                                                }),
                                                        )
                                                        .child(
                                                            div()
                                                                .text_xs()
                                                                .text_color(
                                                                    cx.theme().muted_foreground,
                                                                )
                                                                .child(
                                                                    machine.display_name.clone(),
                                                                ),
                                                        ),
                                                ),
                                        )
                                        .when(selected, |this| {
                                            this.child(
                                                div().pr(px(10.)).child(
                                                    Icon::new(IconName::Check)
                                                        .text_color(cx.theme().green),
                                                ),
                                            )
                                        }),
                                )
                                .on_click(cx.listener(
                                    move |this, _, window, cx| {
                                        this.select_machine(&machine_id, window, cx);
                                    },
                                ))
                            })),
                    ),
            )
    }

    fn render_empty(&self, cx: &Context<Self>) -> impl IntoElement {
        v_flex().size_full().items_center().justify_center().child(
            div()
                .text_sm()
                .text_color(cx.theme().muted_foreground)
                .child("当前没有机器连接到 /sys/info"),
        )
    }
}

impl Render for SysMonitorHostApp {
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
                            .child(
                                div()
                                    .text_sm()
                                    .text_color(cx.theme().foreground)
                                    .child("PxSysMonitorHost"),
                            )
                            .child(
                                div()
                                    .text_xs()
                                    .text_color(cx.theme().muted_foreground)
                                    .child(format!(
                                        "监听 {}{}",
                                        self.server_status.listen_addr,
                                        if self.server_status.last_error.is_empty() {
                                            String::new()
                                        } else {
                                            format!(" | {}", self.server_status.last_error)
                                        }
                                    )),
                            ),
                    )
                    .child(
                        div()
                            .mr_4()
                            .text_xs()
                            .text_color(cx.theme().muted_foreground)
                            .child(format!("状态 {}", self.server_status.state)),
                    ),
            )
            .child(render_connection_summary(
                &format!("WebSocket 路径固定为 {}", PATH_SYS_INFO),
                cx,
            ))
            .child(
                h_flex()
                    .flex_1()
                    .items_start()
                    .child(self.render_sidebar(cx))
                    .child(
                        div().flex_1().h_full().min_h_0().overflow_hidden().child(
                            v_flex()
                                .size_full()
                                .items_start()
                                .when(self.selected_machine().is_none(), |this| {
                                    this.child(self.render_empty(cx))
                                })
                                .when_some(self.selected_machine(), |this, machine| {
                                    this.child(
                                        div()
                                            .px_4()
                                            .py_3()
                                            .text_xs()
                                            .text_color(cx.theme().muted_foreground)
                                            .child(format!(
                                                "{} | {} | 最后上报 {}",
                                                machine.display_name,
                                                machine.peer_addr,
                                                machine.last_seen
                                            )),
                                    )
                                    .child(
                                        TabBar::new("host-monitor-tabs")
                                            .segmented()
                                            .px_3()
                                            .py_2()
                                            .selected_index(active_tab_index)
                                            .on_click(cx.listener(
                                                |this, ix: &usize, window, cx| {
                                                    this.set_active_tab(*ix, window, cx);
                                                },
                                            ))
                                            .child(Tab::new().label("总览"))
                                            .child(Tab::new().label("CPU / 内存"))
                                            .child(Tab::new().label("GPU"))
                                            .child(Tab::new().label("存储"))
                                            .child(Tab::new().label("网络"))
                                            .child(Tab::new().label("传感器")),
                                    )
                                    .child(
                                        div().flex_1().w_full().min_h_0().overflow_hidden().child(
                                            div().size_full().overflow_y_scrollbar().child(
                                                render_dashboard(
                                                    &machine.telemetry,
                                                    self.active_tab,
                                                    cx,
                                                ),
                                            ),
                                        ),
                                    )
                                }),
                        ),
                    ),
            )
    }
}

pub fn run(cli: HostCli) {
    tray::init_tray("PxSysMonitorHost");
    let app = gpui_platform::application().with_assets(Assets);
    app.run(move |cx: &mut App| {
        gpui_component::init(cx);

        let window_options = WindowOptions {
            titlebar: Some(TitleBar::title_bar_options()),
            window_bounds: Some(WindowBounds::centered(size(px(1600.), px(980.)), cx)),
            ..Default::default()
        };

        let cli = cli.clone();
        cx.spawn(async move |cx| {
            let start_hidden = cli.startup;
            let window_handle = cx
                .open_window(window_options, |window, cx| {
                    window.set_window_title("PxSysMonitorHost");

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

                    let host_cli = cli.clone();
                    let view = cx.new(|cx| SysMonitorHostApp::new(window, cx, host_cli));
                    cx.new(|cx| Root::new(view, window, cx))
                })
                .expect("failed to open PxSysMonitorHost window");

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
