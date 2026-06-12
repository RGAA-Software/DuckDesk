export function formatTimestamp(timestamp: number): string {
  const date = new Date(timestamp) // 毫秒级时间戳
  const year = date.getFullYear()
  const month = (date.getMonth() + 1).toString().padStart(2, '0') // 月份从0开始
  const day = date.getDate().toString().padStart(2, '0')
  const hours = date.getHours().toString().padStart(2, '0')
  const minutes = date.getMinutes().toString().padStart(2, '0')
  const seconds = date.getSeconds().toString().padStart(2, '0')

  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`
}

export function formatDuration(ms: number): string {
  if (ms < 0) return '0 ms'

  const totalSeconds = Math.floor(ms / 1000)

  const seconds = totalSeconds % 60
  const totalMinutes = Math.floor(totalSeconds / 60)

  const minutes = totalMinutes % 60
  const hours = Math.floor(totalMinutes / 60)

  const parts: string[] = []

  if (hours > 0) parts.push(`${hours} h`)
  if (minutes > 0) parts.push(`${minutes} m`)
  if (seconds > 0 || parts.length === 0) {
    parts.push(`${seconds} s`)
  }

  return parts.join(' ')
}

/**
 * 将毫秒时间间隔转换为天数格式
 * @param milliseconds 时间间隔（毫秒）
 * @param options 配置选项
 * @returns 格式化后的天数字符串
 */
export function formatTimeToDays(
  milliseconds: number,
  options: {
    precise?: boolean;      // 是否精确显示（包含小时、分钟、秒）
    maxDecimalPlaces?: number; // 小数位数限制（仅当precise为false时有效）
    showZeroDays?: boolean; // 是否显示0天
    unitLabels?: {
      day?: string;
      hour?: string;
      minute?: string;
      second?: string;
    };
  } = {}
): string {
  // 默认配置
  const {
    precise = false,
    maxDecimalPlaces = 2,
    showZeroDays = false,
    unitLabels = {
      day: '天',
      hour: '小时',
      minute: '分钟',
      second: '秒'
    }
  } = options;

  // 验证输入
  if (typeof milliseconds !== 'number' || !Number.isFinite(milliseconds)) {
    return '无效的时间间隔';
  }

  if (milliseconds < 0) {
    return '0' + unitLabels.day;
  }

  const totalSeconds = Math.floor(milliseconds / 1000);
  const totalMinutes = Math.floor(totalSeconds / 60);
  const totalHours = Math.floor(totalMinutes / 60);
  const days = Math.floor(totalHours / 24);

  // 如果不显示0天且天数小于1
  if (!showZeroDays && days < 1 && !precise) {
    const hours = totalHours % 24;
    const minutes = totalMinutes % 60;
    const seconds = totalSeconds % 60;

    // 返回精确的时间格式
    if (hours > 0) {
      return `${hours}${unitLabels.hour}`;
    } else if (minutes > 0) {
      return `${minutes}${unitLabels.minute}`;
    } else {
      return `${seconds}${unitLabels.second}`;
    }
  }

  if (!precise) {
    // 非精确模式：返回小数形式的天数
    const exactDays = milliseconds / (1000 * 60 * 60 * 24);

    if (days === 0 && !showZeroDays) {
      return formatWithDecimalPlaces(exactDays, maxDecimalPlaces) + unitLabels.day;
    }

    return formatWithDecimalPlaces(exactDays, maxDecimalPlaces) + unitLabels.day;
  }

  // 精确模式：返回X天Y小时Z分钟的形式
  const hours = totalHours % 24;
  const minutes = totalMinutes % 60;
  const seconds = totalSeconds % 60;

  const parts: string[] = [];

  if (days > 0 || showZeroDays) {
    parts.push(`${days}${unitLabels.day}`);
  }

  if (hours > 0 || days === 0) {
    parts.push(`${hours}${unitLabels.hour}`);
  }

  if (minutes > 0 && days === 0 && hours === 0) {
    parts.push(`${minutes}${unitLabels.minute}`);
  }

  if (seconds > 0 && days === 0 && hours === 0 && minutes === 0) {
    parts.push(`${seconds}${unitLabels.second}`);
  }

  return parts.join(' ') || `0${unitLabels.second}`;
}

/**
 * 格式化数字，限制小数位数
 */
function formatWithDecimalPlaces(value: number, maxDecimalPlaces: number): string {
  if (Number.isInteger(value)) {
    return value.toString();
  }

  const rounded = Math.round(value * Math.pow(10, maxDecimalPlaces)) / Math.pow(10, maxDecimalPlaces);

  // 移除末尾的0
  return parseFloat(rounded.toFixed(maxDecimalPlaces)).toString();
}

