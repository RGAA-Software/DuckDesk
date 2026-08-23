use crate::auth::console_auth_pull;
use crate::console_settings::ConsoleSettings;
use crate::interact::console_lang::ConsoleLanguage;
use crate::interact::process_manager::{ConsoleProcessManager, LocalProcessState};
use crate::{gAuthManager, gConsoleContext, gConsoleSettings};
use arboard::Clipboard;
use iced::widget::{
    button, column, container, mouse_area, pick_list, row, scrollable, stack, text, Space,
};
use iced::{window, Background, Color, Element, Length, Subscription, Task, Theme};
use px_auth_mgr::authorization::Authorization;
use px_base::ip_util::get_clean_ipv4_addresses;
use px_base::{mongodb_util, redis_util};
use std::sync::Arc;
use std::time::Duration;

const PANEL_WIDTH: f32 = 960.0;
const PANEL_HEIGHT: f32 = 620.0;

pub fn run(
    language: ConsoleLanguage,
    machine_code: String,
    auth: Authorization,
    used_time: i64,
    settings: ConsoleSettings,
) -> iced::Result {
    let icon = window::icon::from_file_data(include_bytes!("../../assets/px_icon.png"), None).ok();
    let window_settings = window::Settings {
        size: iced::Size::new(PANEL_WIDTH, PANEL_HEIGHT),
        min_size: Some(iced::Size::new(820.0, 540.0)),
        icon,
        visible: true,
        ..Default::default()
    };

    iced::application(
        move || {
            ConsolePanel::new(
                language.clone(),
                machine_code.clone(),
                auth.clone(),
                used_time,
                settings.clone(),
            )
        },
        panel_update,
        panel_view,
    )
    .title(panel_title)
    .theme(panel_theme)
    .subscription(panel_subscription)
    .font(include_bytes!("../../assets/MicrosoftYaHeiUILight.ttf").as_slice())
    .window(window_settings)
    // Close requests are converted to a confirmation message. We only close
    // after the local server and media sidecar have been stopped.
    .exit_on_close_request(false)
    .run()
}

#[derive(Debug, Clone)]
enum Message {
    Tick,
    Processes(Result<LocalProcessState, String>),
    Backends { redis_ok: bool, mongodb_ok: bool },
    RefreshAuth,
    AuthRefreshed(Result<(Authorization, i64), String>),
    CopyMachineCode,
    CopyLogin,
    OpenWeb,
    SelectIp(String),
    Restart,
    CloseRequested,
    CancelExit,
    ConfirmExit,
    ExitStopped(LocalProcessState),
}

struct ConsolePanel {
    language: ConsoleLanguage,
    machine_code: String,
    auth: Authorization,
    used_time: i64,
    console_port: u16,
    ssl_enable: bool,
    ips: Vec<String>,
    selected_ip: String,
    processes: Arc<ConsoleProcessManager>,
    process_state: LocalProcessState,
    redis_ok: bool,
    mongodb_ok: bool,
    tick: u8,
    show_exit_dialog: bool,
    exiting: bool,
    notice: Option<String>,
}

impl ConsolePanel {
    fn new(
        language: ConsoleLanguage,
        machine_code: String,
        auth: Authorization,
        used_time: i64,
        settings: ConsoleSettings,
    ) -> Self {
        let ips = get_clean_ipv4_addresses()
            .unwrap_or_default()
            .into_iter()
            .map(|ip| ip.to_string())
            .collect::<Vec<_>>();
        let selected_ip = ips
            .first()
            .cloned()
            .unwrap_or_else(|| "127.0.0.1".to_string());
        let processes = Arc::new(
            ConsoleProcessManager::for_current_exe(&settings.live).unwrap_or_else(|error| {
                panic!("Console process manager initialization failed: {error}")
            }),
        );
        let process_state = processes.snapshot();
        Self {
            language,
            machine_code,
            auth,
            used_time,
            console_port: settings.console_port,
            ssl_enable: settings.ssl_enable,
            ips,
            selected_ip,
            processes,
            process_state,
            redis_ok: false,
            mongodb_ok: false,
            tick: 0,
            show_exit_dialog: false,
            exiting: false,
            notice: None,
        }
    }

    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::Tick if !self.exiting => {
                self.tick = self.tick.wrapping_add(1);
                let processes = self.processes.clone();
                let process_task =
                    Task::perform(async move { processes.reconcile() }, Message::Processes);
                if self.tick % 3 == 0 {
                    Task::batch([
                        process_task,
                        Task::perform(check_backends(), |(redis_ok, mongodb_ok)| {
                            Message::Backends {
                                redis_ok,
                                mongodb_ok,
                            }
                        }),
                    ])
                } else {
                    process_task
                }
            }
            Message::Tick => Task::none(),
            Message::Processes(result) => {
                match result {
                    Ok(state) => self.process_state = state,
                    Err(error) => self.notice = Some(error),
                }
                Task::none()
            }
            Message::Backends {
                redis_ok,
                mongodb_ok,
            } => {
                self.redis_ok = redis_ok;
                self.mongodb_ok = mongodb_ok;
                Task::none()
            }
            Message::RefreshAuth => {
                let machine_code = self.machine_code.clone();
                Task::perform(refresh_authorization(machine_code), Message::AuthRefreshed)
            }
            Message::AuthRefreshed(result) => {
                match result {
                    Ok((auth, used_time)) => {
                        self.auth = auth;
                        self.used_time = used_time;
                        self.notice = Some(self.language.operate_success.clone());
                    }
                    Err(error) => self.notice = Some(format!("授权刷新失败：{error}")),
                }
                Task::none()
            }
            Message::CopyMachineCode => {
                self.notice = copy_to_clipboard(&self.machine_code, &self.language);
                Task::none()
            }
            Message::CopyLogin => {
                let value = if self.auth.username.is_empty() {
                    String::new()
                } else {
                    format!("{} / {}", self.auth.username, self.auth.password)
                };
                self.notice = copy_to_clipboard(&value, &self.language);
                Task::none()
            }
            Message::OpenWeb => {
                let scheme = if self.ssl_enable { "https" } else { "http" };
                let url = format!("{scheme}://{}:{}", self.selected_ip, self.console_port);
                self.notice = match webbrowser::open(&url) {
                    Ok(()) => Some(format!("已打开 {url}")),
                    Err(error) => Some(format!("打开网页失败：{error}")),
                };
                Task::none()
            }
            Message::SelectIp(ip) => {
                self.selected_ip = ip;
                Task::none()
            }
            Message::Restart if !self.exiting => {
                let processes = self.processes.clone();
                Task::perform(async move { processes.restart() }, Message::Processes)
            }
            Message::Restart => Task::none(),
            Message::CloseRequested => {
                self.show_exit_dialog = true;
                Task::none()
            }
            Message::CancelExit => {
                self.show_exit_dialog = false;
                Task::none()
            }
            Message::ConfirmExit => {
                self.exiting = true;
                self.show_exit_dialog = false;
                let processes = self.processes.clone();
                Task::perform(
                    async move { processes.stop_for_exit() },
                    Message::ExitStopped,
                )
            }
            Message::ExitStopped(state) => {
                self.process_state = state;
                iced::exit()
            }
        }
    }

    fn subscription(&self) -> Subscription<Message> {
        Subscription::batch([
            iced::time::every(Duration::from_secs(1)).map(|_| Message::Tick),
            window::close_requests().map(|_| Message::CloseRequested),
        ])
    }

    fn view(&self) -> Element<'_, Message> {
        let auth_text = authorization_text(&self.language, &self.auth, self.used_time);
        let login_text = if self.auth.username.is_empty() {
            "-".to_string()
        } else {
            format!("{} / {}", self.auth.username, self.auth.password)
        };
        let console_status = status_label(self.process_state.console_running());
        let media_status = if self.process_state.media_managed {
            status_label(self.process_state.media_running())
        } else {
            "远端/未托管".to_string()
        };
        let turn_status = status_label(self.process_state.turn_running());

        let running_count = [
            self.process_state.console_running(),
            self.process_state.media_running(),
            self.process_state.turn_running(),
            self.redis_ok && self.mongodb_ok,
        ]
        .into_iter()
        .filter(|running| *running)
        .count();

        let header = row![
            column![
                text("Pixels Console").size(30),
                text("本机服务控制台 · 管理、诊断与访问入口").size(15),
            ]
            .spacing(5),
            Space::new().width(Length::Fill),
            status_pill(format!("{running_count}/4 服务正常"), running_count == 4),
        ]
        .align_y(iced::Alignment::Center)
        .width(Length::Fill);

        let services = card(
            "服务状态",
            "本机进程与数据依赖",
            column![
                service_item(
                    "Console 管理服务",
                    "HTTP 管理、应用调度与 API",
                    console_status,
                    self.process_state.console_running()
                ),
                service_item(
                    "本地媒体服务",
                    "px_media / ZLMediaKit",
                    media_status,
                    self.process_state.media_running()
                ),
                service_item(
                    "TURN 中继服务",
                    "px_turn / Coturn",
                    turn_status,
                    self.process_state.turn_running()
                ),
                service_item(
                    "Redis",
                    "会话与实时状态",
                    status_label(self.redis_ok),
                    self.redis_ok
                ),
                service_item(
                    "MongoDB",
                    "配置与业务数据",
                    status_label(self.mongodb_ok),
                    self.mongodb_ok
                ),
                row![
                    Space::new().width(Length::Fill),
                    button("重启本机 Console")
                        .on_press_maybe((!self.exiting).then_some(Message::Restart))
                ]
                .align_y(iced::Alignment::Center),
            ]
            .spacing(15),
        )
        .width(Length::FillPortion(11));

        let access = card(
            "访问与授权",
            "打开管理网页，或核验授权信息",
            column![
                info_row(
                    "管理网页",
                    pick_list(
                        self.ips.clone(),
                        Some(self.selected_ip.clone()),
                        Message::SelectIp
                    )
                    .width(Length::Fill),
                    button(self.language.open.as_str()).on_press(Message::OpenWeb)
                ),
                info_row(
                    "设备机器码",
                    text(&self.machine_code),
                    button(self.language.copy.as_str()).on_press(Message::CopyMachineCode)
                ),
                info_row(
                    "授权状态",
                    text(auth_text),
                    button(self.language.refresh.as_str()).on_press(Message::RefreshAuth)
                ),
                info_row(
                    "登录账号",
                    text(login_text),
                    button(self.language.copy.as_str()).on_press(Message::CopyLogin)
                ),
            ]
            .spacing(16),
        )
        .width(Length::FillPortion(12));

        let mut page = column![
            header,
            row![services, access]
                .spacing(18)
                .width(Length::Fill)
                .align_y(iced::Alignment::Start),
            exit_area(self.exiting),
        ]
        .spacing(20)
        .padding(28)
        .width(Length::Fill);

        if let Some(notice) = &self.notice {
            page = page.push(
                container(text(notice))
                    .padding([10, 14])
                    .style(container::rounded_box),
            );
        }
        if self.exiting {
            page = page.push(text("正在停止 Console、px_media 与 px_turn…"));
        }

        let base = container(scrollable(page).width(Length::Fill).height(Length::Fill))
            .width(Length::Fill)
            .height(Length::Fill);

        if self.show_exit_dialog {
            stack![base, exit_confirmation()]
                .width(Length::Fill)
                .height(Length::Fill)
                .into()
        } else {
            base.into()
        }
    }
}

fn card<'a>(
    title: &'a str,
    subtitle: &'a str,
    content: impl Into<Element<'a, Message>>,
) -> iced::widget::Container<'a, Message> {
    container(
        column![
            text(title).size(21),
            text(subtitle).size(14),
            content.into()
        ]
        .spacing(8)
        .width(Length::Fill),
    )
    .padding(20)
    .style(container::rounded_box)
}

fn service_item<'a>(
    title: &'a str,
    subtitle: &'a str,
    state: String,
    running: bool,
) -> Element<'a, Message> {
    row![
        column![text(title).size(16), text(subtitle).size(13)].spacing(3),
        Space::new().width(Length::Fill),
        status_pill(state, running),
    ]
    .align_y(iced::Alignment::Center)
    .into()
}

fn info_row<'a>(
    label: &'a str,
    value: impl Into<Element<'a, Message>>,
    action: impl Into<Element<'a, Message>>,
) -> Element<'a, Message> {
    column![
        text(label).size(14),
        row![
            container(value).width(Length::Fill),
            container(action).width(Length::Fixed(82.0)),
        ]
        .spacing(10)
        .align_y(iced::Alignment::Center),
    ]
    .spacing(5)
    .into()
}

fn status_pill<'a>(value: String, ok: bool) -> Element<'a, Message> {
    let color = if ok { text::success } else { text::danger };
    container(text(value).style(color).size(14))
        .padding([5, 10])
        .style(container::rounded_box)
        .into()
}

fn exit_area(exiting: bool) -> Element<'static, Message> {
    container(
        row![
            column![
                text("关闭本机服务").size(16),
                text("退出面板时，将停止本机 Console、px_media 与 px_turn；远端 ZLMediaKit 不受影响。")
                    .size(13),
            ]
            .spacing(3),
            Space::new().width(Length::Fill),
            button(text("退出并停止服务").style(text::danger))
                .on_press_maybe((!exiting).then_some(Message::CloseRequested)),
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([14, 18])
    .style(container::rounded_box)
    .into()
}

fn panel_update(panel: &mut ConsolePanel, message: Message) -> Task<Message> {
    panel.update(message)
}

fn panel_view(panel: &ConsolePanel) -> Element<'_, Message> {
    panel.view()
}

fn panel_subscription(panel: &ConsolePanel) -> Subscription<Message> {
    panel.subscription()
}

fn panel_title(panel: &ConsolePanel) -> String {
    panel.language.app_name.clone()
}

fn panel_theme(_: &ConsolePanel) -> Theme {
    Theme::TokyoNight
}

fn exit_confirmation() -> Element<'static, Message> {
    let dialog = container(
        column![
            text("确认退出").size(25),
            text("是否停止本机 Console 服务？").size(17),
            text("这会一并停止 px_media 与 px_turn；远端 ZLMediaKit 不受影响。").size(14),
            row![
                Space::new().width(Length::Fill),
                button("取消").on_press(Message::CancelExit),
                button(text("停止服务并退出").style(text::danger)).on_press(Message::ConfirmExit),
            ]
            .spacing(12)
            .align_y(iced::Alignment::Center),
        ]
        .spacing(14),
    )
    .width(Length::Fixed(460.0))
    .padding(26)
    .style(container::rounded_box);

    mouse_area(container(dialog).center(Length::Fill).style(|_| {
        container::Style::default()
            .background(Background::Color(Color::from_rgba(0.02, 0.03, 0.08, 0.70)))
    }))
    .on_press(Message::CancelExit)
    .into()
}

fn status_label(ok: bool) -> String {
    if ok {
        "运行中".to_string()
    } else {
        "未运行".to_string()
    }
}

fn authorization_text(language: &ConsoleLanguage, auth: &Authorization, used_time: i64) -> String {
    let mode = if auth.auth_id.is_empty() {
        if language.is_zh_cn() {
            "未授权"
        } else {
            "None"
        }
    } else if language.is_zh_cn() {
        match auth.mode.as_str() {
            "trial" => "试用",
            "licensed" => "正式",
            _ => "未知",
        }
    } else {
        match auth.mode.as_str() {
            "trial" => "Trial",
            "licensed" => "Licensed",
            _ => "Unknown",
        }
    };
    let used_days = used_time / (24 * 60 * 60 * 1000);
    if language.is_zh_cn() {
        format!(
            "流路数: {}, 时间: {}天, 已使用: {}天, 模式: {mode}",
            auth.max_streams, auth.days, used_days
        )
    } else {
        format!(
            "Streams: {}, Days: {}, Used: {} days, Mode: {mode}",
            auth.max_streams, auth.days, used_days
        )
    }
}

fn copy_to_clipboard(value: &str, language: &ConsoleLanguage) -> Option<String> {
    if value.is_empty() {
        return Some(language.copy_failed.clone());
    }
    match Clipboard::new().and_then(|mut clipboard| clipboard.set_text(value)) {
        Ok(()) => Some(language.copy_success.clone()),
        Err(error) => Some(format!("{}: {error}", language.copy_failed)),
    }
}

async fn refresh_authorization(machine_code: String) -> Result<(Authorization, i64), String> {
    gConsoleContext.lock().await.update_machine_code(machine_code);
    console_auth_pull::pull_once()
        .await
        .map_err(|error| error.to_string())?;
    let mut auth_manager = gAuthManager.lock().await;
    auth_manager.load().await;
    let auth = auth_manager.get_auth().await;
    let used_time = auth_manager.get_used_time().await;
    Ok((auth, used_time))
}

async fn check_backends() -> (bool, bool) {
    let (redis_url, mongodb_url) = {
        let settings = gConsoleSettings.lock().await;
        (settings.redis_url.clone(), settings.mongodb_url.clone())
    };
    let redis_ok = redis_util::get_redis_conn_mgr(redis_url).await.is_ok();
    let mongodb_ok = mongodb_util::check_mongodb_available(mongodb_url).await;
    (redis_ok, mongodb_ok)
}
