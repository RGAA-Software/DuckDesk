use crate::interact::spvr_lang::SpvrLanguage;
use crate::{gAuthManager, gSpvrSettings};
use arboard::Clipboard;
use egui::{Color32, RichText};
use egui_notify::Toasts;
use gr_auth_mgr::authorization::Authorization;
use gr_base::ip_util::get_clean_ipv4_addresses;
use gr_base::{mongodb_util, redis_util};

use std::cmp::PartialEq;
use std::fs::File;
use std::io::Read;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use sysinfo::{Pid, ProcessesToUpdate, Signal, System};
use tokio::runtime::Handle;
use webbrowser;

#[derive(Eq, PartialEq)]
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
                    let pid = pid.clone();
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
                            let auth_state: String;
                            let max_streams = self.state.lock().unwrap().auth.max_streams;
                            let days = self.state.lock().unwrap().auth.days;
                            let used_time = self.state.lock().unwrap().used_time;
                            if self.language.is_zh_cn()  {
                                auth_state = format!(
                                    "流路数: {}, 时间: {}天, 已使用: {}天",
                                    max_streams,
                                    days,
                                    milliseconds_to_days(used_time)
                                );
                            } else {
                                auth_state = format!(
                                    "Steams: {}, Days: {}, Used: {}",
                                    max_streams,
                                    days,
                                    milliseconds_to_days(used_time)
                                );
                            }
                            ui.label(auth_state.as_str());

                            // operation
                            if ui
                                .add_sized(
                                    op_btn_size,
                                    egui::Button::new(self.language.refresh.as_str()),
                                )
                                .clicked()
                            {
                                let state = self.state.clone();
                                tokio::task::spawn_blocking(move || {
                                    let rt = Handle::current();
                                    let (auth, used_time) = rt.block_on(refresh_auth());
                                    state.lock().unwrap().auth = auth;
                                    state.lock().unwrap().used_time = used_time;
                                });

                                self.toasts
                                    .success(self.language.operate_success.as_str())
                                    .duration(Duration::from_secs(2));
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
            } else if self.main_page == MainPageType::PageSettings {
                if ui
                    .add_sized([150.0, 32.0], egui::Button::new("Settings"))
                    .clicked()
                {}
            }
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
    // read the auth/auth.info
    gAuthManager.lock().await.load().await;
    let auth = gAuthManager.lock().await.get_auth().await;

    let mut used_time: i64 = 0;
    let file = File::options().read(true).open("au.dat");
    if let Ok(mut file) = file {
        let mut buffer: String = String::default();
        if let Ok(_size) = file.read_to_string(&mut buffer) {
            if let Ok(value) = buffer.parse::<i64>() {
                used_time = value;
            }
        }
    }
    (auth, used_time)
}
