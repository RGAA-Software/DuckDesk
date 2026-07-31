use crate::interact::spvr_lang::SpvrLanguage;
use crate::{gAuthManager, gSpvrContext, gSpvrSettings};
use arboard::Clipboard;
use egui::{Color32, RichText};
use egui_notify::Toasts;
use gr_auth_mgr::authorization::Authorization;
use gr_base::ip_util::get_clean_ipv4_addresses;
use gr_base::{mongodb_util, redis_util};

use std::cmp::PartialEq;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use sysinfo::{Pid, ProcessesToUpdate, Signal, System};
use tokio::runtime::Handle;
use webbrowser;

#[derive(Eq, PartialEq)]
#[allow(clippy::enum_variant_names)]
enum MainPageType {
    PageServerSettings = 0,
    PageServerState,
    PageRelayServer,
    PageSettings,
}

#[derive(Debug, Clone)]
pub struct SpvrUIState {
    pub spvr_alive: bool,
    pub spvr_alive_pid: u32,
    pub relay_alive: bool,
    pub auth: Authorization,
    pub used_time: i64,
    pub redis_ok: bool,
    pub mongodb_ok: bool,
    pub show_exit_dialog: bool,
}

pub struct SpvrUI {
    toasts: Toasts,
    main_page: MainPageType,
    language: SpvrLanguage,
    machine_code: String,
    state: Arc<Mutex<SpvrUIState>>,
    selected_ip: String,
    total_ips: Vec<String>,
    /// “刷新状态”按钮触发的授权 pull 结果回传通道（避免阻塞 UI 线程）。
    pull_result_rx: Option<std::sync::mpsc::Receiver<Result<(), String>>>,
}

impl SpvrUI {
    pub fn setup_custom_fonts(ctx: &egui::Context) {
        use egui::{FontFamily, FontId, TextStyle};
        let mut fonts = egui::FontDefinitions::default();

        fonts.font_data.insert(
            "my_font".to_owned(),
            Arc::from(egui::FontData::from_owned(
                include_bytes!("../../assets/MicrosoftYaHeiUILight.ttf").to_vec(),
            )),
        );

        fonts
            .families
            .get_mut(&FontFamily::Proportional)
            .unwrap()
            .insert(0, "my_font".to_owned());

        let mut style = (*ctx.style()).clone();
        style
            .text_styles
            .insert(TextStyle::Body, FontId::new(18.0, FontFamily::Proportional));
        ctx.set_style(style);
        ctx.set_fonts(fonts);
    }

    pub fn new(
        cc: &eframe::CreationContext<'_>,
        language: SpvrLanguage,
        machine_code: String,
        state: Arc<Mutex<SpvrUIState>>,
    ) -> Self {
        SpvrUI::setup_custom_fonts(&cc.egui_ctx);

        let mut selected_ip = "".to_string();
        let mut ip_array = Vec::new();
        let ips = get_clean_ipv4_addresses();
        if let Ok(ips) = ips {
            for ip in ips {
                tracing::info!("===> IP: {}", ip.to_string());
                ip_array.push(ip.to_string());
            }
        }
        if !ip_array.is_empty() {
            selected_ip = ip_array[0].clone();
        }

        let s = SpvrUI {
            toasts: Default::default(),
            main_page: MainPageType::PageServerSettings,
            language,
            machine_code,
            state,
            selected_ip,
            total_ips: ip_array,
            pull_result_rx: None,
        };
        s.check_server_state();
        tracing::error!("will connect redis to test it 000");
        s.check_redis_mongodb_state();
        s
    }

    pub fn check_server_state(&self) {
        let state = self.state.clone();
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(std::time::Duration::from_secs(1));

                let my_pid = std::process::id();
                let mut sys = System::new_all();
                sys.refresh_all();
                let processes = sys.processes();
                state.lock().unwrap().spvr_alive = false;
                state.lock().unwrap().relay_alive = false;
                for (pid, process) in processes {
                    let name = process.name().to_string_lossy().to_string().to_lowercase();
                    if name.contains("gr_relay") {
                        state.lock().unwrap().relay_alive = true;
                    }
                    let pid = *pid;
                    if name.contains("gr_cms_server") && pid.as_u32() != my_pid {
                        state.lock().unwrap().spvr_alive = true;
                        state.lock().unwrap().spvr_alive_pid = pid.as_u32();
                    }
                }

                if !state.lock().unwrap().spvr_alive {
                    state.lock().unwrap().spvr_alive_pid = 0;
                    // start it
                    let current_exe = std::env::current_exe().unwrap();
                    // 如果路径包含空格或特殊字符，可能需要转换为字符串
                    let exe_str = current_exe.to_string_lossy();
                    let _ = std::process::Command::new(exe_str.to_string())
                        .args(["-r=server"])
                        .spawn();
                }

                std::thread::sleep(std::time::Duration::from_secs(3));
            }
        });
    }

    pub fn check_redis_mongodb_state(&self) {
        let state = self.state.clone();
        tokio::spawn(async move {
            loop {
                // check redis
                let redis_url = gSpvrSettings.lock().await.redis_url.clone();
                let redis_conn = redis_util::get_redis_conn_mgr(redis_url.clone()).await;
                if let Err(err) = redis_conn {
                    tracing::error!("connect to redis failed: {}", err.to_string());
                    state.lock().unwrap().redis_ok = false;
                } else {
                    state.lock().unwrap().redis_ok = true;
                }

                // check mongodb
                let mongodb_uri = gSpvrSettings.lock().await.mongodb_url.clone();
                state.lock().unwrap().mongodb_ok =
                    mongodb_util::check_mongodb_available(mongodb_uri).await;

                tokio::time::sleep(std::time::Duration::from_secs(3)).await;
            }
        });
    }

    pub fn kill_server(&self) {
        // kill the spvr server
        let spvr_pid = self.state.lock().unwrap().spvr_alive_pid;
        if spvr_pid > 0 {
            let mut sys = System::new_all();
            sys.refresh_processes(ProcessesToUpdate::All, true);
            // 查找进程
            if let Some(process) = sys.process(Pid::from_u32(spvr_pid)) {
                // 发送终止信号
                let _ = process.kill_with(Signal::Kill).is_some();
                tracing::warn!("will close : {}", spvr_pid);
            }
        }
    }
}

impl eframe::App for SpvrUI {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // dark
        ctx.set_visuals(egui::Visuals::dark());

        egui::SidePanel::left("side_panel").show(ctx, |ui| {
            // left
            ui.vertical_centered(|ui| {
                ui.add_space(20.);
                let btn_size = [150.0, 32.0];
                // logo
                ui.add(
                    egui::Image::new(egui::include_image!("../../assets/tc_icon.png"))
                        .fit_to_exact_size(egui::Vec2::new(55.0, 55.0))
                        .corner_radius(10),
                );

                ui.add_space(20.);
                if ui
                    .add_sized(
                        btn_size,
                        egui::Button::new(self.language.server_settings.as_str())
                            .selected(self.main_page == MainPageType::PageServerSettings),
                    )
                    .clicked()
                {
                    self.main_page = MainPageType::PageServerSettings;
                }

                //ui.add_space(15.);
                //if ui.add_sized(btn_size, egui::SelectableLabel::new(self.main_page == MainPageType::PageServerState, self.language.server_state.as_str())).clicked() {
                //    self.main_page = MainPageType::PageServerState;
                //}
            });
        });
        egui::CentralPanel::default().show(ctx, |ui| {
            if self.main_page == MainPageType::PageServerSettings {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    let op_btn_size = [70.0, 25.0];
                    ui.add_space(15.0);
                    ui.heading(self.language.server_settings.as_str());
                    ui.add_space(15.0);

                    egui::Grid::new("basic_info")
                        .num_columns(3)
                        .spacing([40.0, 14.0])
                        .striped(true)
                        .show(ui, |ui| {
                            // label
                            ui.label(self.language.st_server_id.as_str());

                            // value
                            ui.label(
                                RichText::new(self.machine_code.as_str())
                                    .strong()
                                    .color(Color32::WHITE)
                                    .background_color(Color32::DARK_BLUE),
                            );

                            // operation
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.copy.as_str()),
                                )
                                .clicked()
                            {
                                let mut ok = false;
                                if let Ok(mut clipboard) = Clipboard::new() {
                                    if let Ok(_r) = clipboard.set_text(self.machine_code.as_str()) {
                                        self.toasts
                                            .success(self.language.copy_success.as_str())
                                            .duration(Duration::from_secs(2));
                                        ok = true;
                                    }
                                }
                                if !ok {
                                    self.toasts
                                        .error(self.language.copy_success.as_str())
                                        .duration(Duration::from_secs(2));
                                }
                            }
                            ui.end_row();

                            // authorization
                            fn milliseconds_to_days(millis: i64) -> i64 {
                                millis / (24 * 60 * 60 * 1000)
                            }

                            ui.label(self.language.st_auth_state.as_str());
                            let (max_streams, days, used_time, auth_id_empty, mode) = {
                                let s = self.state.lock().unwrap();
                                (
                                    s.auth.max_streams,
                                    s.auth.days,
                                    s.used_time,
                                    s.auth.auth_id.is_empty(),
                                    s.auth.mode.clone(),
                                )
                            };
                            let mode_text = if auth_id_empty {
                                if self.language.is_zh_cn() { "未授权" } else { "None" }
                            } else if self.language.is_zh_cn() {
                                match mode.as_str() {
                                    "trial" => "试用",
                                    "licensed" => "正式",
                                    _ => "未知",
                                }
                            } else {
                                match mode.as_str() {
                                    "trial" => "Trial",
                                    "licensed" => "Licensed",
                                    _ => "Unknown",
                                }
                            };
                            let auth_state = if self.language.is_zh_cn() {
                                format!(
                                    "流路数: {}, 时间: {}天, 已使用: {}天, 模式: {}",
                                    max_streams,
                                    days,
                                    milliseconds_to_days(used_time),
                                    mode_text
                                )
                            } else {
                                format!(
                                    "Steams: {}, Days: {}, Used: {}, Mode: {}",
                                    max_streams,
                                    days,
                                    milliseconds_to_days(used_time),
                                    mode_text
                                )
                            };
                            ui.label(auth_state.as_str());

                            // operation — 刷新状态：触发一次网络授权 pull（不阻塞 UI 线程）
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.refresh.as_str()),
                                )
                                .clicked()
                            {
                                let state = self.state.clone();
                                let machine_code = self.machine_code.clone();
                                let (tx, rx) = std::sync::mpsc::channel::<Result<(), String>>();
                                self.pull_result_rx = Some(rx);
                                tokio::task::spawn_blocking(move || {
                                    let rt = Handle::current();
                                    rt.block_on(async move {
                                        // 面板进程：machine_code 需先写入 context
                                        //（server 进程在启动时已写入）。
                                        gSpvrContext.lock().await.update_machine_code(machine_code);
                                        // 触发一次网络拉取。面板进程不持有 KvStorage
                                        //（sled 由 server 进程独占），pull 结果只更新
                                        // 本进程内存中的授权用于展示。
                                        let r = crate::auth::spvr_auth_pull::pull_once()
                                            .await
                                            .map(|_| ());
                                        if let Err(e) = &r {
                                            tracing::error!("refresh: pull authorization failed: {}", e);
                                        }
                                        let (auth, used_time) = refresh_auth().await;
                                        state.lock().unwrap().auth = auth;
                                        state.lock().unwrap().used_time = used_time;
                                        let _ = tx.send(r);
                                    });
                                });
                            }

                            ui.end_row();

                            // web 登录账号（license 携带的 username/password，
                            // 仅本机面板可见，用于登录 CMS web 管理页）
                            ui.label(if self.language.is_zh_cn() {
                                "登录账号"
                            } else {
                                "Login Account"
                            });
                            let (login_user, login_pwd) = {
                                let s = self.state.lock().unwrap();
                                (s.auth.username.clone(), s.auth.password.clone())
                            };
                            let login_text = if login_user.is_empty() {
                                "-".to_string()
                            } else {
                                format!("{} / {}", login_user, login_pwd)
                            };
                            ui.label(
                                RichText::new(login_text.as_str())
                                    .strong()
                                    .color(Color32::WHITE)
                                    .background_color(Color32::DARK_BLUE),
                            );
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.copy.as_str()),
                                )
                                .clicked()
                            {
                                let mut ok = false;
                                if !login_user.is_empty() {
                                    if let Ok(mut clipboard) = Clipboard::new() {
                                        if let Ok(_r) = clipboard.set_text(login_text.as_str()) {
                                            self.toasts
                                                .success(self.language.copy_success.as_str())
                                                .duration(Duration::from_secs(2));
                                            ok = true;
                                        }
                                    }
                                }
                                if !ok {
                                    self.toasts
                                        .error(self.language.copy_success.as_str())
                                        .duration(Duration::from_secs(2));
                                }
                            }
                            ui.end_row();

                            // update auth
                            //ui.label(self.language.st_update_auth.as_str());
                            //ui.label("");
                            //// operation
                            //if ui.add_sized(op_btn_size, egui::Button::new(self.language.update.as_str())).clicked() {

                            //}
                            //ui.end_row();

                            // manager server
                            ui.label(self.language.st_spvr_state.as_str());
                            if self.state.lock().unwrap().spvr_alive {
                                ui.label(
                                    RichText::new("OK")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_GREEN),
                                );
                            } else {
                                ui.label(
                                    RichText::new("ERROR")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_RED),
                                );
                            }

                            // operation
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.restart.as_str()),
                                )
                                .clicked()
                            {
                                self.toasts
                                    .success(self.language.operate_success.as_str())
                                    .duration(Duration::from_secs(5));
                                // kill the spvr server
                                self.kill_server();
                            }
                            ui.end_row();

                            // open manager website
                            //
                            ui.label(self.language.st_spvr_website.as_str());
                            egui::ComboBox::from_id_salt("http_ips")
                                .selected_text(&self.selected_ip)
                                .show_ui(ui, |ui| {
                                    for fruit in &self.total_ips {
                                        ui.selectable_value(
                                            &mut self.selected_ip,
                                            fruit.clone(),
                                            fruit,
                                        );
                                    }
                                });

                            // operation
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.open.as_str()),
                                )
                                .clicked()
                            {
                                // open the site
                                if webbrowser::open(
                                    format!("http://{}:30499", self.selected_ip).as_str(),
                                )
                                .is_ok()
                                {
                                    // ...
                                }
                            }
                            ui.end_row();

                            // redis server state
                            //
                            ui.label(self.language.redis_state.as_str());
                            if self.state.lock().unwrap().redis_ok {
                                ui.label(
                                    RichText::new("OK")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_GREEN),
                                );
                            } else {
                                ui.label(
                                    RichText::new("ERROR")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_RED),
                                );
                            }
                            ui.end_row();

                            // mongodb server state
                            //
                            ui.label(self.language.mongodb_state.as_str());
                            if self.state.lock().unwrap().mongodb_ok {
                                ui.label(
                                    RichText::new("OK")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_GREEN),
                                );
                            } else {
                                ui.label(
                                    RichText::new("ERROR")
                                        .strong()
                                        .color(Color32::WHITE)
                                        .background_color(Color32::DARK_RED),
                                );
                            }
                            ui.end_row();

                            // Exit the server
                            //
                            //
                            ui.label(self.language.st_exit_server.as_str());
                            ui.label("");
                            // operation
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(
                                        RichText::new(self.language.exit.as_str())
                                            .color(Color32::RED),
                                    ),
                                )
                                .clicked()
                            {
                                // open the site
                                self.state.lock().unwrap().show_exit_dialog = true;
                            }
                            ui.end_row();
                        });

                    // 授权 pull 结果回传（channel，非阻塞轮询）
                    let pull_result = self
                        .pull_result_rx
                        .as_ref()
                        .and_then(|rx| rx.try_recv().ok());
                    if let Some(result) = pull_result {
                        match result {
                            Ok(()) => {
                                self.toasts
                                    .success(self.language.operate_success.as_str())
                                    .duration(Duration::from_secs(2));
                            }
                            Err(e) => {
                                self.toasts
                                    .error(format!("pull failed: {e}"))
                                    .duration(Duration::from_secs(4));
                            }
                        }
                        self.pull_result_rx = None;
                    }

                    self.toasts.show(ctx);
                });
            } else if self.main_page == MainPageType::PageServerState {
                if ui
                    .add_sized([150.0, 32.0], egui::Button::new("Profile Server"))
                    .clicked()
                {}
            } else if self.main_page == MainPageType::PageRelayServer {
                if ui
                    .add_sized([150.0, 32.0], egui::Button::new("Relay Server"))
                    .clicked()
                {}
            } else if self.main_page == MainPageType::PageSettings
                && ui
                    .add_sized([150.0, 32.0], egui::Button::new("Settings"))
                    .clicked()
                {}
        });

        if self.state.lock().unwrap().show_exit_dialog {
            egui::Window::new(self.language.st_exit_server.as_str())
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .fixed_size([320.0, 160.0])
                .show(ctx, |ui| {
                    ui.vertical_centered(|ui| {
                        ui.add_space(20.0);

                        ui.label(
                            RichText::new(self.language.st_ask_exit_server.as_str()).size(20.0),
                        );
                        ui.add_space(20.0);

                        // 方法2：使用弹性空间将按钮推到中间
                        ui.horizontal(|ui| {
                            // 左侧添加弹性空间
                            ui.add_space(ui.available_width() / 2.0 - 77.5); // (70+70+15)/2 = 77.5

                            if ui
                                .add_sized(
                                    [70.0, 25.0],
                                    egui::Button::new(self.language.cancel.as_str()),
                                )
                                .clicked()
                            {
                                self.state.lock().unwrap().show_exit_dialog = false;
                            }

                            ui.add_space(15.0);

                            if ui
                                .add_sized(
                                    [70.0, 25.0],
                                    egui::Button::new(
                                        egui::RichText::new(self.language.sure.as_str())
                                            .color(egui::Color32::WHITE),
                                    )
                                    .fill(egui::Color32::from_rgb(200, 0, 0)),
                                )
                                .clicked()
                            {
                                self.kill_server();
                                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                            }
                        });
                    });
                });
        }

        ctx.request_repaint();
    }
}

async fn refresh_auth() -> (Authorization, i64) {
    // reload the cached authorization (KvStorage; 面板进程未初始化 KvStorage 时
    // load 为空操作，保留 pull_once 写入的内存授权)
    gAuthManager.lock().await.load().await;
    let auth = gAuthManager.lock().await.get_auth().await;

    // 已使用时间由服务器锚定的有效期推导（见 AuthManager::get_used_time）
    let used_time = gAuthManager.lock().await.get_used_time().await;
    (auth, used_time)
}
