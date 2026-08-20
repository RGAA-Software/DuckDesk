use gpui::{
    div, linear_color_stop, linear_gradient, prelude::FluentBuilder as _, px, AnyElement, Context,
    Hsla, InteractiveElement, IntoElement, ParentElement, SharedString, Styled,
};
use gpui_component::{
    chart::AreaChart,
    h_flex,
    progress::Progress,
    table::{Table, TableBody, TableCaption, TableCell, TableHead, TableHeader, TableRow},
    v_flex, ActiveTheme, StyledExt,
};

use crate::monitor_model::{
    bytes_to_gb, chart_ticks, cpu_color, disk_usage_percent, format_optional_frequency,
    format_tick, gpu_key, gpu_memory_percent, network_ipv4, sensor_status, MachineTelemetry,
    MonitorTab,
};

pub fn render_dashboard<V>(
    telemetry: &MachineTelemetry,
    active_tab: MonitorTab,
    cx: &Context<V>,
) -> AnyElement {
    match active_tab {
        MonitorTab::Overview => render_overview_tab(telemetry, cx).into_any_element(),
        MonitorTab::CpuMemory => render_cpu_memory_tab(telemetry, cx).into_any_element(),
        MonitorTab::Gpu => render_gpu_tab(telemetry, cx).into_any_element(),
        MonitorTab::Storage => render_storage_tab(telemetry, cx).into_any_element(),
        MonitorTab::Network => render_network_tab(telemetry, cx).into_any_element(),
        MonitorTab::Sensors => render_sensors_tab(telemetry, cx).into_any_element(),
    }
}

pub fn render_connection_summary<V>(details: &str, cx: &Context<V>) -> impl IntoElement {
    div()
        .px_4()
        .py_2()
        .text_xs()
        .text_color(cx.theme().muted_foreground)
        .bg(cx.theme().tab_bar)
        .child(details.to_string())
}

fn render_metric_card<V>(
    id: &str,
    title: &str,
    value: String,
    percent: Option<f32>,
    cx: &Context<V>,
) -> impl IntoElement {
    v_flex()
        .id(SharedString::from(id.to_string()))
        .gap_2()
        .p_3()
        .flex_1()
        .min_w(px(180.))
        .rounded_md()
        .border_1()
        .border_color(cx.theme().border)
        .bg(cx.theme().secondary)
        .child(
            div()
                .text_xs()
                .text_color(cx.theme().muted_foreground)
                .child(title.to_string()),
        )
        .child(
            div()
                .text_lg()
                .text_color(cx.theme().foreground)
                .child(value),
        )
        .when_some(percent, |this, percent| {
            this.child(
                Progress::new(SharedString::from(format!("{id}-progress")))
                    .w_full()
                    .h_2()
                    .value(percent.clamp(0.0, 100.0)),
            )
        })
}

#[allow(clippy::too_many_arguments)]
fn render_chart<V, T: Clone + 'static>(
    id: &str,
    title: &str,
    data: Vec<T>,
    x_fn: impl Fn(&T) -> String + 'static,
    y_fn: impl Fn(&T) -> f64 + 'static,
    color: Hsla,
    unit: &str,
    x_unit: &str,
    y_max: Option<f64>,
    cx: &Context<V>,
) -> impl IntoElement {
    let current_value = data.last().map(&y_fn).unwrap_or(0.0);
    let max_value = data
        .iter()
        .map(&y_fn)
        .fold(0.0_f64, f64::max)
        .max(y_max.unwrap_or(0.0))
        .max(1.0);
    let tick_values = chart_ticks(max_value);
    let mut chart = AreaChart::new(data)
        .x(x_fn)
        .y(y_fn)
        .linear()
        .stroke(color)
        .fill(linear_gradient(
            0.,
            linear_color_stop(color.opacity(0.40), 1.),
            linear_color_stop(cx.theme().background.opacity(0.05), 0.),
        ))
        .tick_margin(14);
    if let Some(y_max) = y_max {
        chart = chart
            .y(move |_| y_max)
            .stroke(color.opacity(0.0))
            .fill(cx.theme().background.opacity(0.0));
    }

    v_flex()
        .id(SharedString::from(id.to_string()))
        .min_h(px(260.))
        .gap_3()
        .p_4()
        .rounded_md()
        .border_1()
        .border_color(cx.theme().border)
        .bg(cx.theme().secondary)
        .child(
            div()
                .text_base()
                .font_semibold()
                .text_color(cx.theme().foreground)
                .child(title.to_string()),
        )
        .child(
            div()
                .text_sm()
                .text_color(cx.theme().muted_foreground)
                .child(format!(
                    "当前 {:.1} {} | Y轴单位 {} | X轴单位 {}",
                    current_value, unit, unit, x_unit
                )),
        )
        .child(
            h_flex()
                .gap_3()
                .flex_1()
                .child(
                    v_flex()
                        .w(px(52.))
                        .h_full()
                        .justify_between()
                        .items_end()
                        .pt_3()
                        .pb_8()
                        .pr_1()
                        .children(tick_values.iter().map(|value| {
                            div()
                                .text_xs()
                                .text_color(cx.theme().muted_foreground)
                                .child(format_tick(*value, unit))
                        })),
                )
                .child(div().flex_1().h_full().child(chart)),
        )
        .child(
            h_flex()
                .justify_between()
                .items_center()
                .child(
                    div()
                        .text_xs()
                        .text_color(cx.theme().muted_foreground)
                        .child(format!("Y: {unit}")),
                )
                .child(
                    div()
                        .text_xs()
                        .text_color(cx.theme().muted_foreground)
                        .child(format!("X: {x_unit} ->")),
                ),
        )
}

fn render_overview_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    let history: Vec<_> = telemetry.history.iter().cloned().collect();
    v_flex()
        .gap_4()
        .p_4()
        .child(
            h_flex()
                .gap_3()
                .flex_wrap()
                .child(render_metric_card(
                    "overview-cpu",
                    "CPU 使用率",
                    format!("{:.1}%", telemetry.latest_cpu_percent()),
                    Some(telemetry.latest_cpu_percent()),
                    cx,
                ))
                .child(render_metric_card(
                    "overview-mem",
                    "内存使用",
                    format!(
                        "{:.1} / {:.1} GB",
                        bytes_to_gb(telemetry.current.mem.used),
                        bytes_to_gb(telemetry.current.mem.total)
                    ),
                    Some(telemetry.latest_mem_percent()),
                    cx,
                ))
                .child(render_metric_card(
                    "overview-disk",
                    "磁盘最高占用",
                    format!("{:.1}%", telemetry.highest_disk_percent()),
                    Some(telemetry.highest_disk_percent()),
                    cx,
                ))
                .child(render_metric_card(
                    "overview-gpu",
                    "首块 GPU",
                    if telemetry.current.gpus.is_empty() {
                        "无 GPU 数据".to_string()
                    } else {
                        format!("{:.0}%", telemetry.primary_gpu_percent())
                    },
                    if telemetry.current.gpus.is_empty() {
                        None
                    } else {
                        Some(telemetry.primary_gpu_percent())
                    },
                    cx,
                ))
                .child(render_metric_card(
                    "overview-send",
                    "主网卡发送速率",
                    format!("{:.2} MB/s", telemetry.latest_send_rate()),
                    None,
                    cx,
                ))
                .child(render_metric_card(
                    "overview-recv",
                    "主网卡接收速率",
                    format!("{:.2} MB/s", telemetry.latest_recv_rate()),
                    None,
                    cx,
                )),
        )
        .child(
            h_flex()
                .gap_4()
                .flex_wrap()
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "overview-chart-cpu",
                    "CPU 趋势",
                    history.clone(),
                    |point| point.time.clone(),
                    |point| point.cpu_usage,
                    cx.theme().red,
                    "%",
                    "时间",
                    Some(100.0),
                    cx,
                )))
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "overview-chart-mem",
                    "内存占用趋势",
                    history.clone(),
                    |point| point.time.clone(),
                    |point| point.mem_usage_percent,
                    cx.theme().blue,
                    "%",
                    "时间",
                    Some(100.0),
                    cx,
                ))),
        )
        .when(!telemetry.current.gpus.is_empty(), |this| {
            this.child(
                v_flex().gap_3().child(section_title("GPU 总览", cx)).child(
                    v_flex()
                        .gap_4()
                        .children(telemetry.current.gpus.iter().map(|gpu| {
                            let gpu_id = gpu_key(gpu);
                            let gpu_name = gpu.brand.clone();
                            let mem_total_gb = gpu.mem_total_gb as f64;
                            let history = telemetry
                                .gpu_history
                                .get(&gpu_id)
                                .map(|items| items.iter().cloned().collect::<Vec<_>>())
                                .unwrap_or_default();

                            h_flex()
                                .gap_4()
                                .flex_wrap()
                                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                                    &format!("overview-gpu-chart-usage-{gpu_id}"),
                                    &format!("{gpu_name} GPU 利用率"),
                                    history.clone(),
                                    |point| point.time.clone(),
                                    |point| point.usage,
                                    cx.theme().blue,
                                    "%",
                                    "时间",
                                    Some(100.0),
                                    cx,
                                )))
                                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                                    &format!("overview-gpu-chart-memory-{gpu_id}"),
                                    &format!("{gpu_name} 显存利用率"),
                                    history,
                                    |point| point.time.clone(),
                                    move |point| {
                                        if mem_total_gb <= 0.0 {
                                            0.0
                                        } else {
                                            (point.memory_used_gb / mem_total_gb) * 100.0
                                        }
                                    },
                                    cx.theme().green,
                                    "%",
                                    "时间",
                                    Some(100.0),
                                    cx,
                                )))
                        })),
                ),
            )
        })
        .child(
            h_flex()
                .gap_4()
                .flex_wrap()
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "overview-chart-send",
                    "网络发送速率",
                    history.clone(),
                    |point| point.time.clone(),
                    |point| point.net_send_mb,
                    cx.theme().green,
                    "MB/s",
                    "时间",
                    None,
                    cx,
                )))
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "overview-chart-recv",
                    "网络接收速率",
                    history,
                    |point| point.time.clone(),
                    |point| point.net_recv_mb,
                    cx.theme().yellow,
                    "MB/s",
                    "时间",
                    None,
                    cx,
                ))),
        )
}

fn render_cpu_memory_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    let history: Vec<_> = telemetry.history.iter().cloned().collect();
    v_flex()
        .gap_4()
        .p_4()
        .child(
            h_flex()
                .gap_4()
                .flex_wrap()
                .child(
                    v_flex()
                        .gap_3()
                        .flex_1()
                        .min_w(px(320.))
                        .p_3()
                        .rounded_md()
                        .border_1()
                        .border_color(cx.theme().border)
                        .bg(cx.theme().secondary)
                        .child(section_title("CPU 概览", cx))
                        .child(kv_line("型号", &telemetry.current.cpu.brand, cx))
                        .child(kv_line("Vendor", &telemetry.current.cpu.vendor, cx))
                        .child(kv_line(
                            "基准频率",
                            &format!("{:.2} GHz", telemetry.current.cpu.base_frequency),
                            cx,
                        ))
                        .child(kv_line(
                            "最大频率",
                            &format!("{:.2} GHz", telemetry.current.cpu.max_frequency),
                            cx,
                        ))
                        .child(kv_line(
                            "当前频率",
                            &format_optional_frequency(telemetry.current.cpu.current_frequency),
                            cx,
                        ))
                        .child(kv_line(
                            "逻辑核心数",
                            &telemetry.current.cpu.cpus.len().to_string(),
                            cx,
                        ))
                        .child(kv_line(
                            "总使用率",
                            &format!("{:.1}%", telemetry.current.cpu.usage),
                            cx,
                        )),
                )
                .child(
                    v_flex()
                        .gap_3()
                        .flex_1()
                        .min_w(px(320.))
                        .p_3()
                        .rounded_md()
                        .border_1()
                        .border_color(cx.theme().border)
                        .bg(cx.theme().secondary)
                        .child(section_title("内存概览", cx))
                        .child(kv_line(
                            "总内存",
                            &format!("{:.1} GB", bytes_to_gb(telemetry.current.mem.total)),
                            cx,
                        ))
                        .child(kv_line(
                            "已用内存",
                            &format!("{:.1} GB", bytes_to_gb(telemetry.current.mem.used)),
                            cx,
                        ))
                        .child(kv_line(
                            "可用内存",
                            &format!("{:.1} GB", bytes_to_gb(telemetry.current.mem.available)),
                            cx,
                        ))
                        .child(kv_line(
                            "使用率",
                            &format!("{:.1}%", telemetry.latest_mem_percent()),
                            cx,
                        ))
                        .child(
                            Progress::new("mem-total-progress")
                                .w_full()
                                .h_2()
                                .value(telemetry.latest_mem_percent()),
                        ),
                ),
        )
        .child(
            h_flex()
                .gap_4()
                .flex_wrap()
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "cpu-chart",
                    "CPU 使用率",
                    history.clone(),
                    |point| point.time.clone(),
                    |point| point.cpu_usage,
                    cx.theme().red,
                    "%",
                    "时间",
                    Some(100.0),
                    cx,
                )))
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "mem-chart",
                    "内存已用",
                    history,
                    |point| point.time.clone(),
                    |point| point.mem_used_gb,
                    cx.theme().blue,
                    "GB",
                    "时间",
                    Some(bytes_to_gb(telemetry.current.mem.total)),
                    cx,
                ))),
        )
        .child(
            v_flex()
                .gap_2()
                .p_3()
                .rounded_md()
                .border_1()
                .border_color(cx.theme().border)
                .bg(cx.theme().secondary)
                .child(section_title("每核心使用率", cx))
                .children(
                    telemetry
                        .current
                        .cpu
                        .cpus
                        .iter()
                        .enumerate()
                        .map(|(index, cpu)| {
                            h_flex()
                                .gap_3()
                                .items_center()
                                .child(
                                    div()
                                        .min_w(px(120.))
                                        .text_xs()
                                        .text_color(cx.theme().muted_foreground)
                                        .child(format!("Core {index}")),
                                )
                                .child(
                                    Progress::new(SharedString::from(format!("cpu-core-{index}")))
                                        .w_full()
                                        .h_2()
                                        .value(cpu.usage.clamp(0.0, 100.0)),
                                )
                                .child(
                                    div()
                                        .min_w(px(64.))
                                        .text_xs()
                                        .text_color(cpu_color(cpu.usage, cx.theme()))
                                        .child(format!("{:.1}%", cpu.usage)),
                                )
                        }),
                ),
        )
}

fn render_gpu_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    v_flex()
        .gap_4()
        .p_4()
        .when(telemetry.current.gpus.is_empty(), |this| {
            this.child(empty_state("当前没有 GPU 监控数据", cx))
        })
        .children(telemetry.current.gpus.iter().map(|gpu| {
            let gpu_id = gpu_key(gpu);
            let history = telemetry
                .gpu_history
                .get(&gpu_id)
                .map(|items| items.iter().cloned().collect::<Vec<_>>())
                .unwrap_or_default();

            v_flex()
                .gap_4()
                .p_3()
                .rounded_md()
                .border_1()
                .border_color(cx.theme().border)
                .bg(cx.theme().secondary)
                .child(section_title(&gpu.brand, cx))
                .child(
                    h_flex()
                        .gap_3()
                        .flex_wrap()
                        .child(render_metric_card(
                            &format!("gpu-{}-usage", gpu_id),
                            "利用率",
                            format!("{}%", gpu.gpu_utilization),
                            Some(gpu.gpu_utilization as f32),
                            cx,
                        ))
                        .child(render_metric_card(
                            &format!("gpu-{}-temp", gpu_id),
                            "温度",
                            format!("{} °C", gpu.temperature),
                            None,
                            cx,
                        ))
                        .child(render_metric_card(
                            &format!("gpu-{}-fan", gpu_id),
                            "风扇转速",
                            format!("{} RPM", gpu.fan_speed),
                            None,
                            cx,
                        ))
                        .child(render_metric_card(
                            &format!("gpu-{}-vram", gpu_id),
                            "显存",
                            format!("{:.2} / {:.2} GB", gpu.mem_used_gb, gpu.mem_total_gb),
                            Some(gpu_memory_percent(gpu)),
                            cx,
                        ))
                        .child(render_metric_card(
                            &format!("gpu-{}-encoder", gpu_id),
                            "编码器",
                            format!("{}%", gpu.encoder_utilization),
                            Some(gpu.encoder_utilization as f32),
                            cx,
                        ))
                        .child(render_metric_card(
                            &format!("gpu-{}-power", gpu_id),
                            "功耗上限",
                            format!("{:.1} W", gpu.power_limit as f64 / 1000.0),
                            None,
                            cx,
                        )),
                )
                .child(
                    h_flex()
                        .gap_4()
                        .flex_wrap()
                        .child(div().flex_1().min_w(px(280.)).child(render_chart(
                            &format!("gpu-{}-chart-usage", gpu_id),
                            "GPU 利用率",
                            history.clone(),
                            |point| point.time.clone(),
                            |point| point.usage,
                            cx.theme().blue,
                            "%",
                            "时间",
                            Some(100.0),
                            cx,
                        )))
                        .child(div().flex_1().min_w(px(280.)).child(render_chart(
                            &format!("gpu-{}-chart-temp", gpu_id),
                            "GPU 温度",
                            history.clone(),
                            |point| point.time.clone(),
                            |point| point.temperature,
                            cx.theme().red,
                            "°C",
                            "时间",
                            Some(100.0),
                            cx,
                        )))
                        .child(div().flex_1().min_w(px(280.)).child(render_chart(
                            &format!("gpu-{}-chart-mem", gpu_id),
                            "显存占用",
                            history,
                            |point| point.time.clone(),
                            |point| point.memory_used_gb,
                            cx.theme().green,
                            "GB",
                            "时间",
                            Some(gpu.mem_total_gb as f64),
                            cx,
                        ))),
                )
                .child(kv_line("唯一标识", &gpu.id, cx))
        }))
}

fn render_storage_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    v_flex()
        .gap_4()
        .p_4()
        .child(
            h_flex()
                .gap_3()
                .flex_wrap()
                .child(render_metric_card(
                    "storage-disk-count",
                    "磁盘数量",
                    telemetry.current.disks.len().to_string(),
                    None,
                    cx,
                ))
                .child(render_metric_card(
                    "storage-max-used",
                    "最高占用",
                    format!("{:.1}%", telemetry.highest_disk_percent()),
                    Some(telemetry.highest_disk_percent()),
                    cx,
                )),
        )
        .child(render_disk_table(telemetry, cx))
}

fn render_network_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    let history: Vec<_> = telemetry.history.iter().cloned().collect();
    let primary = telemetry.current.networks.first();
    v_flex()
        .gap_4()
        .p_4()
        .child(
            h_flex()
                .gap_3()
                .flex_wrap()
                .child(render_metric_card(
                    "network-primary-name",
                    "当前主网卡",
                    primary
                        .map(|item| item.name.clone())
                        .unwrap_or_else(|| "未识别".to_string()),
                    None,
                    cx,
                ))
                .child(render_metric_card(
                    "network-send-rate",
                    "发送速率",
                    format!("{:.2} MB/s", telemetry.latest_send_rate()),
                    None,
                    cx,
                ))
                .child(render_metric_card(
                    "network-recv-rate",
                    "接收速率",
                    format!("{:.2} MB/s", telemetry.latest_recv_rate()),
                    None,
                    cx,
                )),
        )
        .child(
            h_flex()
                .gap_4()
                .flex_wrap()
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "network-chart-send",
                    "发送速率",
                    history.clone(),
                    |point| point.time.clone(),
                    |point| point.net_send_mb,
                    cx.theme().green,
                    "MB/s",
                    "时间",
                    None,
                    cx,
                )))
                .child(div().flex_1().min_w(px(320.)).child(render_chart(
                    "network-chart-recv",
                    "接收速率",
                    history,
                    |point| point.time.clone(),
                    |point| point.net_recv_mb,
                    cx.theme().yellow,
                    "MB/s",
                    "时间",
                    None,
                    cx,
                ))),
        )
        .child(render_network_table(telemetry, cx))
}

fn render_sensors_tab<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    v_flex()
        .gap_4()
        .p_4()
        .when(telemetry.current.components.is_empty(), |this| {
            this.child(empty_state("当前没有温度传感器数据", cx))
        })
        .when(!telemetry.current.components.is_empty(), |this| {
            let max_temp = telemetry
                .current
                .components
                .iter()
                .map(|component| component.temperature)
                .fold(0.0, f32::max);
            this.child(
                h_flex()
                    .gap_3()
                    .flex_wrap()
                    .child(render_metric_card(
                        "sensor-count",
                        "传感器数量",
                        telemetry.current.components.len().to_string(),
                        None,
                        cx,
                    ))
                    .child(render_metric_card(
                        "sensor-max",
                        "最高当前温度",
                        format!("{max_temp:.1} °C"),
                        None,
                        cx,
                    )),
            )
            .child(render_sensor_table(telemetry, cx))
        })
}

fn render_disk_table<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    Table::new()
        .child(
            TableHeader::new().child(
                TableRow::new()
                    .child(TableHead::new().child("挂载点"))
                    .child(TableHead::new().child("类型"))
                    .child(TableHead::new().child("文件系统"))
                    .child(TableHead::new().text_right().child("总量(GB)"))
                    .child(TableHead::new().text_right().child("可用(GB)"))
                    .child(TableHead::new().text_right().child("已用(%)")),
            ),
        )
        .child(
            TableBody::new().children(telemetry.current.disks.iter().map(|disk| {
                TableRow::new()
                    .child(TableCell::new().child(disk.mount_on.clone()))
                    .child(TableCell::new().child(disk.disk_type.clone()))
                    .child(TableCell::new().child(disk.filesystem.clone()))
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{}", disk.total_gb)),
                    )
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{}", disk.available_gb)),
                    )
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.1}%", disk_usage_percent(disk))),
                    )
            })),
        )
        .child(TableCaption::new().child(format!("共 {} 块磁盘", telemetry.current.disks.len())))
        .bg(cx.theme().table)
}

fn render_network_table<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    Table::new()
        .child(
            TableHeader::new().child(
                TableRow::new()
                    .child(TableHead::new().child("名称"))
                    .child(TableHead::new().child("MAC"))
                    .child(TableHead::new().child("IPv4"))
                    .child(TableHead::new().text_right().child("累计发送(GB)"))
                    .child(TableHead::new().text_right().child("累计接收(GB)"))
                    .child(TableHead::new().text_right().child("链路速率(Mbps)")),
            ),
        )
        .child(
            TableBody::new().children(telemetry.current.networks.iter().map(|network| {
                TableRow::new()
                    .child(TableCell::new().child(network.name.clone()))
                    .child(TableCell::new().child(network.mac.clone()))
                    .child(TableCell::new().child(network_ipv4(network)))
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.2}", bytes_to_gb(network.sent_data))),
                    )
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.2}", bytes_to_gb(network.received_data))),
                    )
                    .child(TableCell::new().text_right().child(format!(
                        "{}/{}",
                        network.max_transmit_speed, network.max_receive_speed
                    )))
            })),
        )
        .child(TableCaption::new().child("仅展示 px_sysinfo 当前识别到的主网卡数据"))
        .bg(cx.theme().table)
}

fn render_sensor_table<V>(telemetry: &MachineTelemetry, cx: &Context<V>) -> impl IntoElement {
    Table::new()
        .child(
            TableHeader::new().child(
                TableRow::new()
                    .child(TableHead::new().child("标签"))
                    .child(TableHead::new().text_right().child("当前温度"))
                    .child(TableHead::new().text_right().child("最大"))
                    .child(TableHead::new().text_right().child("临界"))
                    .child(TableHead::new().child("状态")),
            ),
        )
        .child(
            TableBody::new().children(telemetry.current.components.iter().map(|component| {
                TableRow::new()
                    .child(TableCell::new().child(component.label.clone()))
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.1} °C", component.temperature)),
                    )
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.1} °C", component.max)),
                    )
                    .child(
                        TableCell::new()
                            .text_right()
                            .child(format!("{:.1} °C", component.critical)),
                    )
                    .child(TableCell::new().child(sensor_status(component)))
            })),
        )
        .child(TableCaption::new().child(format!(
            "共 {} 个温度组件",
            telemetry.current.components.len()
        )))
        .bg(cx.theme().table)
}

fn section_title<V>(title: &str, cx: &Context<V>) -> impl IntoElement {
    div()
        .text_sm()
        .text_color(cx.theme().foreground)
        .child(title.to_string())
}

fn kv_line<V>(key: &str, value: &str, cx: &Context<V>) -> impl IntoElement {
    h_flex()
        .justify_between()
        .gap_3()
        .child(
            div()
                .text_xs()
                .text_color(cx.theme().muted_foreground)
                .child(key.to_string()),
        )
        .child(
            div()
                .text_xs()
                .text_color(cx.theme().foreground)
                .child(value.to_string()),
        )
}

fn empty_state<V>(message: &str, cx: &Context<V>) -> impl IntoElement {
    div()
        .p_4()
        .rounded_md()
        .border_1()
        .border_color(cx.theme().border)
        .bg(cx.theme().secondary)
        .text_sm()
        .text_color(cx.theme().muted_foreground)
        .child(message.to_string())
}
