pub fn format_duration(milliseconds: u64) -> String {
    // 定义常量
    const MS_PER_SECOND: u64 = 1000;
    const MS_PER_MINUTE: u64 = MS_PER_SECOND * 60;
    const MS_PER_HOUR: u64 = MS_PER_MINUTE * 60;
    const MS_PER_DAY: u64 = MS_PER_HOUR * 24;

    // 计算各个时间单位
    let days = milliseconds / MS_PER_DAY;
    let hours = (milliseconds % MS_PER_DAY) / MS_PER_HOUR;
    let minutes = (milliseconds % MS_PER_HOUR) / MS_PER_MINUTE;
    let seconds = (milliseconds % MS_PER_MINUTE) / MS_PER_SECOND;
    let remaining_ms = milliseconds % MS_PER_SECOND;

    // 根据值决定是否显示，并格式化为字符串
    let mut parts = Vec::new();

    if days > 0 {
        parts.push(format!("{}天", days));
    }
    if hours > 0 {
        parts.push(format!("{}小时", hours));
    }
    if minutes > 0 {
        parts.push(format!("{}分钟", minutes));
    }
    if seconds > 0 {
        parts.push(format!("{}秒", seconds));
    }
    if remaining_ms > 0 || parts.is_empty() {
        parts.push(format!("{}毫秒", remaining_ms));
    }

    // 将部分连接成一个字符串
    parts.join("")
}
