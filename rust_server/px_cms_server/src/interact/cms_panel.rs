use crate::auth::cms_auth_pull;
use crate::cms_settings::CmsSettings;
use crate::interact::cms_lang::CmsLanguage;
use crate::interact::process_manager::{CmsProcessManager, LocalProcessState};
use crate::{gAuthManager, gCmsContext, gCmsSettings};
use arboard::Clipboard;
use iced::widget::{button, column, container, pick_list, row, scrollable, text, Space};
use iced::{window, Element, Length, Subscription, Task, Theme};
use px_auth_mgr::authorization::Authorization;
use px_base::ip_util::get_clean_ipv4_addresses;
use px_base::{mongodb_util, redis_util};
use std::sync::Arc;
use std::time::Duration;

const PANEL_WIDTH: f32 = 960.0;
const PANEL_HEIGHT: f32 = 540.0;

pub fn run(
    language: CmsLanguage,
    machine_code: String,
    auth: Authorization,
    used_time: i64,
    settings: CmsSettings,
) -> iced::Result {
    let icon = window::icon::from_file_data(include_bytes!("../../assets/px_icon.png"), None).ok();
    let window_settings = window::Settings {
        size: iced::Size::new(PANEL_WIDTH, PANEL_HEIGHT),
        min_size: Some(iced::Size::new(760.0, 440.0)),
        icon,
        ..Default::default()
    };

    iced::application(
        move || {
            CmsPanel::new(
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

struct CmsPanel {
    language: CmsLanguage,
    machine_code: String,
    auth: Authorization,
    used_time: i64,
    cms_port: u16,
    ssl_enable: bool,
    ips: Vec<String>,
    selected_ip: String,
    processes: Arc<CmsProcessManager>,
    process_state: LocalProcessState,
    redis_ok: bool,
    mongodb_ok: bool,
    tick: u8,
    show_exit_dialog: bool,
    exiting: bool,
    notice: Option<String>,
}

impl CmsPanel {
    fn new(
        language: CmsLanguage,
        machine_code: String,
        auth: Authorization,
        used_time: i64,
        settings: CmsSettings,
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
            CmsProcessManager::for_current_exe(&settings.live).unwrap_or_else(|error| {
                panic!("CMS process manager initialization failed: {error}")
            }),
        );
        let process_state = processes.snapshot();
        Self {
            language,
            machine_code,
            auth,
            used_time,
            cms_port: settings.cms_port,
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
                let url = format!("{scheme}://{}:{}", self.selected_ip, self.cms_port);
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
        let cms_status = status_label(self.process_state.cms_running());
        let media_status = if self.process_state.media_managed {
            status_label(self.process_state.media_running())
        } else {
            "远端/未托管".to_string()
        };

        let rows = column![
            setting_row(
                &self.language.st_server_id,
                text(&self.machine_code),
                button(self.language.copy.as_str()).on_press(Message::CopyMachineCode)
            ),
            setting_row(
                &self.language.st_auth_state,
                text(auth_text),
                button(self.language.refresh.as_str()).on_press(Message::RefreshAuth)
            ),
            setting_row(
                "登录账号",
                text(login_text),
                button(self.language.copy.as_str()).on_press(Message::CopyLogin)
            ),
            setting_row(
                &self.language.st_cms_state,
                colored_status(cms_status, self.process_state.cms_running()),
                button(self.language.restart.as_str())
                    .on_press_maybe((!self.exiting).then_some(Message::Restart))
            ),
            setting_row(
                "本地媒体服务",
                colored_status(media_status, self.process_state.media_running()),
                Space::new()
            ),
            setting_row(
                &self.language.st_cms_website,
                pick_list(
                    self.ips.clone(),
                    Some(self.selected_ip.clone()),
                    Message::SelectIp
                )
                .width(Length::Fill),
                button(self.language.open.as_str()).on_press(Message::OpenWeb)
            ),
            setting_row(
                &self.language.redis_state,
                colored_status(
                    if self.redis_ok {
                        "运行中".to_string()
                    } else {
                        "未运行".to_string()
                    },
                    self.redis_ok
                ),
                Space::new()
            ),
            setting_row(
                &self.language.mongodb_state,
                colored_status(
                    if self.mongodb_ok {
                        "运行中".to_string()
                    } else {
                        "未运行".to_string()
                    },
                    self.mongodb_ok
                ),
                Space::new()
            ),
            setting_row(
                &self.language.st_exit_server,
                text("将停止本地 CMS 与 px_media"),
                button(text(&self.language.exit).style(text::danger))
                    .on_press(Message::CloseRequested)
            ),
        ]
        .spacing(14)
        .width(Length::Fill);

        let mut body = column![text(&self.language.server_settings).size(28), rows]
            .spacing(24)
            .padding(28)
            .width(Length::Fill);
        if let Some(notice) = &self.notice {
            body = body.push(
                container(text(notice))
                    .padding(10)
                    .style(container::rounded_box),
            );
        }
        if self.show_exit_dialog {
            body = body.push(exit_confirmation(&self.language));
        }
        if self.exiting {
            body = body.push(text("正在停止 CMS 与本地 px_media…"));
        }

        container(scrollable(body).width(Length::Fill).height(Length::Fill))
            .width(Length::Fill)
            .height(Length::Fill)
            .into()
    }
}

fn setting_row<'a>(
    label: &'a str,
    value: impl Into<Element<'a, Message>>,
    action: impl Into<Element<'a, Message>>,
) -> Element<'a, Message> {
    row![
        container(text(label)).width(Length::Fixed(150.0)),
        container(value).width(Length::Fill),
        container(action).width(Length::Fixed(90.0)),
    ]
    .spacing(16)
    .align_y(iced::Alignment::Center)
    .into()
}

fn colored_status(value: String, ok: bool) -> Element<'static, Message> {
    if ok {
        text(value).style(text::success).into()
    } else {
        text(value).style(text::danger).into()
    }
}

fn panel_update(panel: &mut CmsPanel, message: Message) -> Task<Message> {
    panel.update(message)
}

fn panel_view(panel: &CmsPanel) -> Element<'_, Message> {
    panel.view()
}

fn panel_subscription(panel: &CmsPanel) -> Subscription<Message> {
    panel.subscription()
}

fn panel_title(panel: &CmsPanel) -> String {
    panel.language.app_name.clone()
}

fn panel_theme(_: &CmsPanel) -> Theme {
    Theme::TokyoNight
}

fn exit_confirmation(language: &CmsLanguage) -> Element<'_, Message> {
    container(
        column![
            text(&language.st_ask_exit_server).size(20),
            text("这会停止本机的 CMS 服务与由它托管的 px_media。远端 ZLMediaKit 不受影响。"),
            row![
                button(language.cancel.as_str()).on_press(Message::CancelExit),
                button(language.sure.as_str()).on_press(Message::ConfirmExit),
            ]
            .spacing(12),
        ]
        .spacing(14),
    )
    .padding(18)
    .style(container::rounded_box)
    .into()
}

fn status_label(ok: bool) -> String {
    if ok {
        "运行中".to_string()
    } else {
        "未运行".to_string()
    }
}

fn authorization_text(language: &CmsLanguage, auth: &Authorization, used_time: i64) -> String {
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

fn copy_to_clipboard(value: &str, language: &CmsLanguage) -> Option<String> {
    if value.is_empty() {
        return Some(language.copy_failed.clone());
    }
    match Clipboard::new().and_then(|mut clipboard| clipboard.set_text(value)) {
        Ok(()) => Some(language.copy_success.clone()),
        Err(error) => Some(format!("{}: {error}", language.copy_failed)),
    }
}

async fn refresh_authorization(machine_code: String) -> Result<(Authorization, i64), String> {
    gCmsContext.lock().await.update_machine_code(machine_code);
    cms_auth_pull::pull_once()
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
        let settings = gCmsSettings.lock().await;
        (settings.redis_url.clone(), settings.mongodb_url.clone())
    };
    let redis_ok = redis_util::get_redis_conn_mgr(redis_url).await.is_ok();
    let mongodb_ok = mongodb_util::check_mongodb_available(mongodb_url).await;
    (redis_ok, mongodb_ok)
}
