// 游戏手柄采集与发送:浏览器 Gamepad API -> media_data_channel -> kGamepadState
// 协议对齐:
//   - tc_message.proto: kHello=0(Hello.enable_controller=4), kGamepadState=80,
//     GamepadState{buttons,left_trigger,right_trigger,thumb_lx/ly/rx/ry,gp_type}
//   - src/gr_render/plugins/joystick/joystick_plugin.cpp:
//     收到 kHello{enable_controller=true} 后按 stream_id 分配 ViGEm 虚拟 X360 手柄;
//     收到 kGamepadState 后把字段直接填进 XINPUT_GAMEPAD(=XUSB_REPORT) 回放,
//     gp_type 在回放路径上被忽略,固定填 kButtons(0)
// 数值范围对齐 XInput:buttons 为 XInput 位掩码,扳机 0~255,摇杆 -32768~32767

export const MSG_TYPE_HELLO = 0 // kHello
export const MSG_TYPE_GAMEPAD_STATE = 80 // kGamepadState
const CLIENT_TYPE_UNKNOWN = 100 // tc_message.proto ClientType.kUnknown

// XInput 按键位掩码(tc_message.proto GamepadButton)
export const GP = {
  DPAD_UP: 0x0001,
  DPAD_DOWN: 0x0002,
  DPAD_LEFT: 0x0004,
  DPAD_RIGHT: 0x0008,
  START: 0x0010,
  BACK: 0x0020,
  LEFT_THUMB: 0x0040,
  RIGHT_THUMB: 0x0080,
  LEFT_SHOULDER: 0x0100,
  RIGHT_SHOULDER: 0x0200,
  A: 0x1000,
  B: 0x2000,
  X: 0x4000,
  Y: 0x8000,
} as const

// W3C standard mapping 按键序号 -> XInput 位掩码
// (6=LT / 7=RT 是模拟量,走 left_trigger/right_trigger,不进 buttons)
const BUTTON_MASKS: Record<number, number> = {
  0: GP.A,
  1: GP.B,
  2: GP.X,
  3: GP.Y,
  4: GP.LEFT_SHOULDER,
  5: GP.RIGHT_SHOULDER,
  8: GP.BACK,
  9: GP.START,
  10: GP.LEFT_THUMB,
  11: GP.RIGHT_THUMB,
  12: GP.DPAD_UP,
  13: GP.DPAD_DOWN,
  14: GP.DPAD_LEFT,
  15: GP.DPAD_RIGHT,
}

// 摇杆死区:低于该值的模拟量直接归零(浏览器噪声)
const STICK_DEADZONE = 0.04
// 模拟量变化阈值:归一化变化超过 0.02 才发送(按键沿不受限)
const ANALOG_SEND_THRESHOLD = 0.02
const THUMB_MAX = 32767
const TRIGGER_MAX = 255

export interface GamepadSnapshot {
  buttons: number
  leftTrigger: number // 0~255
  rightTrigger: number // 0~255
  thumbLx: number // -32768~32767
  thumbLy: number
  thumbRx: number
  thumbRy: number
}

export const ZERO_SNAPSHOT: GamepadSnapshot = {
  buttons: 0,
  leftTrigger: 0,
  rightTrigger: 0,
  thumbLx: 0,
  thumbLy: 0,
  thumbRx: 0,
  thumbRy: 0,
}

export interface GamepadOptions {
  // 发送 tc.Message 字段(App.vue 的 sendControl,内部完成 TLV 打包)
  send: (fields: Record<string, unknown>) => boolean
  onLog?: (msg: string) => void
  // 状态文本变化回调(连接的手柄名 / 未检测到)
  onStatus?: (text: string) => void
  // 轮询间隔,默认 16ms(~60Hz)
  pollIntervalMs?: number
}

function axisToThumb(v: number): number {
  // 浏览器摇杆 -1.0~1.0,Y 轴向下为正;XInput 摇杆 -32768~32767,Y 轴向上为正 -> 取反
  const clamped = Math.abs(v) < STICK_DEADZONE ? 0 : Math.max(-1, Math.min(1, v))
  return Math.round(clamped * THUMB_MAX)
}

function triggerToByte(v: number): number {
  const clamped = Math.max(0, Math.min(1, v))
  return Math.round(clamped * TRIGGER_MAX)
}

// Gamepad(standard mapping) -> XInput 快照;无手柄返回 null
export function snapshotFrom(pad: Gamepad | null): GamepadSnapshot | null {
  if (!pad) return null
  let buttons = 0
  pad.buttons.forEach((b, i) => {
    const mask = BUTTON_MASKS[i]
    if (mask && b.pressed) buttons |= mask
  })
  const axes = pad.axes
  return {
    buttons,
    // LT/RT:standard mapping 是 buttons[6]/[7] 的模拟值;数字手柄 fallback pressed
    leftTrigger: triggerToByte(pad.buttons[6]?.value ?? (pad.buttons[6]?.pressed ? 1 : 0)),
    rightTrigger: triggerToByte(pad.buttons[7]?.value ?? (pad.buttons[7]?.pressed ? 1 : 0)),
    thumbLx: axisToThumb(axes[0] ?? 0),
    thumbLy: axisToThumb(-(axes[1] ?? 0)),
    thumbRx: axisToThumb(axes[2] ?? 0),
    thumbRy: axisToThumb(-(axes[3] ?? 0)),
  }
}

function analogChanged(a: GamepadSnapshot, b: GamepadSnapshot): boolean {
  const thumbDelta = ANALOG_SEND_THRESHOLD * THUMB_MAX
  const triggerDelta = ANALOG_SEND_THRESHOLD * TRIGGER_MAX
  return (
    Math.abs(a.thumbLx - b.thumbLx) > thumbDelta ||
    Math.abs(a.thumbLy - b.thumbLy) > thumbDelta ||
    Math.abs(a.thumbRx - b.thumbRx) > thumbDelta ||
    Math.abs(a.thumbRy - b.thumbRy) > thumbDelta ||
    Math.abs(a.leftTrigger - b.leftTrigger) > triggerDelta ||
    Math.abs(a.rightTrigger - b.rightTrigger) > triggerDelta
  )
}

export class GamepadController {
  enabled = false

  private opts: GamepadOptions
  private timer: number | null = null
  private padIndex: number | null = null
  private last: GamepadSnapshot | null = null
  private statusText = ''

  constructor(opts: GamepadOptions) {
    this.opts = opts
  }

  status(): string {
    return this.statusText
  }

  // 开启:先通知 render 分配虚拟手柄(kHello enable_controller),再开始轮询
  enable(): boolean {
    if (this.enabled) return true
    const ok = this.opts.send({
      type: MSG_TYPE_HELLO,
      hello: {
        enableAudio: true,
        enableVideo: true,
        enableController: true,
        clientType: CLIENT_TYPE_UNKNOWN,
        deviceName: 'gr_web_client',
      },
    })
    if (!ok) {
      this.opts.onLog?.('数据通道未连接,无法开启手柄')
      return false
    }
    this.enabled = true
    window.addEventListener('gamepadconnected', this.onGamepadConnected)
    window.addEventListener('gamepaddisconnected', this.onGamepadDisconnected)
    this.last = null
    this.timer = window.setInterval(this.poll, this.opts.pollIntervalMs ?? 16)
    this.opts.onLog?.('手柄回传已开启(已请求远端分配虚拟手柄)')
    this.poll()
    return true
  }

  // 关闭:补发一帧全零状态,避免远端按键/摇杆卡住
  disable() {
    if (!this.enabled) return
    this.enabled = false
    if (this.timer !== null) {
      window.clearInterval(this.timer)
      this.timer = null
    }
    window.removeEventListener('gamepadconnected', this.onGamepadConnected)
    window.removeEventListener('gamepaddisconnected', this.onGamepadDisconnected)
    if (this.last && (this.last.buttons !== 0 || analogChanged(this.last, ZERO_SNAPSHOT))) {
      this.sendState(ZERO_SNAPSHOT, true)
    }
    this.last = null
    this.padIndex = null
    this.setStatus('')
    this.opts.onLog?.('手柄回传已关闭')
  }

  // 发送一帧状态(force=false 时仅在相对上一帧有变化时发送)
  sendState(s: GamepadSnapshot, force = false): boolean {
    if (!force && this.last) {
      // 按键沿:任何按键变化必发;模拟量:超过阈值才发
      if (s.buttons === this.last.buttons && !analogChanged(s, this.last)) return true
    }
    const ok = this.opts.send({
      type: MSG_TYPE_GAMEPAD_STATE,
      gamepadState: {
        buttons: s.buttons,
        leftTrigger: s.leftTrigger,
        rightTrigger: s.rightTrigger,
        thumbLx: s.thumbLx,
        thumbLy: s.thumbLy,
        thumbRx: s.thumbRx,
        thumbRy: s.thumbRy,
        gpType: 0, // GamepadState.GamepadType.kButtons,回放侧忽略
      },
    })
    if (ok) this.last = { ...s }
    return ok
  }

  // 轮询一次(默认由定时器驱动;CDP 调试可手动调用)
  poll = () => {
    if (!this.enabled) return
    let pad: Gamepad | null = null
    let pads: (Gamepad | null)[] = []
    try {
      pads = Array.from(navigator.getGamepads?.() ?? [])
    } catch {
      pads = []
    }
    if (this.padIndex !== null) {
      pad = pads[this.padIndex] ?? null
      if (!pad?.connected) {
        pad = null
        this.padIndex = null
      }
    }
    if (!pad) {
      pad = pads.find((p) => p && p.connected) ?? null
      this.padIndex = pad ? pad.index : null
    }
    if (!pad) {
      this.setStatus('未检测到手柄')
      return
    }
    this.setStatus(pad.id || `Gamepad #${pad.index}`)
    const s = snapshotFrom(pad)
    if (s) this.sendState(s)
  }

  private onGamepadConnected = (ev: GamepadEvent) => {
    this.setStatus(ev.gamepad.id || `Gamepad #${ev.gamepad.index}`)
    this.opts.onLog?.(`手柄已连接: ${ev.gamepad.id} (#${ev.gamepad.index})`)
  }

  private onGamepadDisconnected = (ev: GamepadEvent) => {
    this.opts.onLog?.(`手柄已断开: ${ev.gamepad.id} (#${ev.gamepad.index})`)
    if (this.padIndex === ev.gamepad.index) {
      this.padIndex = null
      // 断开前补发全零,释放远端按住的键
      if (this.last) this.sendState(ZERO_SNAPSHOT, true)
      this.last = null
    }
    this.setStatus('未检测到手柄')
  }

  private setStatus(text: string) {
    if (text === this.statusText) return
    this.statusText = text
    this.opts.onStatus?.(text)
  }
}
