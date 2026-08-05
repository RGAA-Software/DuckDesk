// 鼠标/键盘输入采集与发送:经 input_data_channel 发送 NetTlvHeader + tc.Message
// 协议对齐 src/gr_client/front_render/ct_video_widget.cpp 的 SendMouseEvent/SendKeyEvent
import { packTlv } from './tlv'
import { VK_MAP, isNumLockRelated, isCapsLockRelated } from './vk_map'
import {
  encodeMessage,
  MSG_TYPE_KEY_EVENT,
  MSG_TYPE_MOUSE_EVENT,
  LOCK_KEY_DONT_CARE,
  LOCK_KEY_CHECK_NUM_LOCK,
  LOCK_KEY_CHECK_CAPS_LOCK,
  BTN_LEFT_UP,
  BTN_MIDDLE_UP,
  BTN_RIGHT_UP,
  BTN_MOUSE_MOVE,
  BTN_WHEEL,
  BTN_LEFT_DOWN,
  BTN_MIDDLE_DOWN,
  BTN_RIGHT_DOWN,
} from './proto'

export interface InputOptions {
  dc: RTCDataChannel
  deviceId: string
  streamId: string
  monitorName: string
  video: HTMLVideoElement
  onLog?: (msg: string) => void
}

// datachannel 发送缓冲超过该值时丢弃纯移动事件(按键/点击/滚轮不丢)
const DROP_MOVE_BUFFERED_BYTES = 256 * 1024

// 触屏手势阈值
const TOUCH_TAP_MAX_MOVE_PX = 12 // 位移超过此值视为拖动,取消 tap/长按
const TOUCH_LONG_PRESS_MS = 500 // 单指长按判定时间(=右键)
const TOUCH_TWO_FINGER_TAP_MAX_MS = 400 // 双指 tap 最大持续时长(=中键)

const DOWN_FLAGS: Record<number, number> = { 0: BTN_LEFT_DOWN, 1: BTN_MIDDLE_DOWN, 2: BTN_RIGHT_DOWN }
const UP_FLAGS: Record<number, number> = { 0: BTN_LEFT_UP, 1: BTN_MIDDLE_UP, 2: BTN_RIGHT_UP }

// 触屏手势状态机:none -> single(单指) -> two(双指) -> two-ending(双指已抬一指) -> rest(剩余手指忽略直到全部抬起)
type TouchMode = 'none' | 'single' | 'two' | 'two-ending' | 'rest'

type MovePos = { x: number; y: number; dx?: number; dy?: number }

export class InputController {
  viewOnly = false
  // 指针锁定(相对鼠标)模式:mousemove 改发相对位移换算后的坐标
  // 注:回放侧 win_event_replayer.cpp 忽略 delta_x/delta_y(始终 MOUSEEVENTF_ABSOLUTE),
  // ButtonFlag 也无相对移动标志位,故页内维护虚拟光标位置,换算成绝对坐标发送;
  // deltaX/deltaY 字段顺带填上原始位移(回放侧目前忽略,仅供前向兼容/调试)
  relativeMode = false
  // 最近一次发出的鼠标事件字段(CDP 调试用)
  lastMouse: Record<string, unknown> | null = null
  // 诊断计数:DOM 鼠标移动事件总数 / 实际发出的输入消息总数(性能面板算速率)
  domMoveEvents = 0
  sentMessages = 0

  private opts: InputOptions
  private pktIndex = 0n
  private attached = false
  // rAF 合并:同帧内多次 mousemove 只在帧末补发最新点;帧内首次立即发送
  private pendingMove: MovePos | null = null
  private rafPending = false
  private moveSentThisFrame = false
  // 相对模式虚拟光标位置(0~1 比例坐标,夹紧)
  private virtX = 0.5
  private virtY = 0.5
  private wheelAcc = 0
  // ---- 触屏手势状态 ----
  private touchMode: TouchMode = 'none'
  private touchStartX = 0
  private touchStartY = 0
  private touchMoved = false
  private longPressFired = false
  private longPressTimer: number | null = null
  private twoStartAt = 0
  private twoMoved = false
  private twoLastMidX = 0
  private twoLastMidY = 0
  private twoWheelAcc = 0
  // 视频几何缓存:避免每个 mousemove 强制 layout(getBoundingClientRect)
  private cachedRect: DOMRect | null = null
  private cachedDispW = 0
  private cachedDispH = 0
  private cachedOffX = 0
  private cachedOffY = 0
  private geoValid = false
  private resizeObserver: ResizeObserver | null = null

  // 切屏后更新回放坐标系(render 按 monitorName 定位屏幕几何)
  setMonitorName(name: string) {
    this.opts.monitorName = name
  }

  constructor(opts: InputOptions) {
    this.opts = opts
  }

  attach() {
    if (this.attached) return
    this.attached = true
    const v = this.opts.video
    // mousemove 高频路径:不 preventDefault,用默认冒泡即可;不做 capture(避免抢其它监听)
    v.addEventListener('mousemove', this.onMouseMove)
    v.addEventListener('mousedown', this.onMouseDown)
    v.addEventListener('mouseup', this.onMouseUp)
    v.addEventListener('wheel', this.onWheel, { passive: false })
    v.addEventListener('contextmenu', this.onContextMenu)
    // 触屏手势:单指拖动=移动 / 单指tap=左键 / 单指长按=右键 / 双指拖动=滚轮 / 双指tap=中键
    v.addEventListener('touchstart', this.onTouchStart, { passive: false })
    v.addEventListener('touchmove', this.onTouchMove, { passive: false })
    v.addEventListener('touchend', this.onTouchEnd, { passive: false })
    v.addEventListener('touchcancel', this.onTouchCancel, { passive: false })
    window.addEventListener('keydown', this.onKeyDown)
    window.addEventListener('keyup', this.onKeyUp)
    // 页面失焦时补发修饰键 release,防止远端按键卡死
    window.addEventListener('blur', this.onBlur)
    v.addEventListener('resize', this.invalidateGeometry)
    window.addEventListener('resize', this.invalidateGeometry)
    if (typeof ResizeObserver !== 'undefined') {
      this.resizeObserver = new ResizeObserver(() => this.invalidateGeometry())
      this.resizeObserver.observe(v)
    }
    this.refreshGeometry()
  }

  detach() {
    if (!this.attached) return
    this.attached = false
    const v = this.opts.video
    v.removeEventListener('mousemove', this.onMouseMove)
    v.removeEventListener('mousedown', this.onMouseDown)
    v.removeEventListener('mouseup', this.onMouseUp)
    v.removeEventListener('wheel', this.onWheel)
    v.removeEventListener('contextmenu', this.onContextMenu)
    v.removeEventListener('touchstart', this.onTouchStart)
    v.removeEventListener('touchmove', this.onTouchMove)
    v.removeEventListener('touchend', this.onTouchEnd)
    v.removeEventListener('touchcancel', this.onTouchCancel)
    window.removeEventListener('keydown', this.onKeyDown)
    window.removeEventListener('keyup', this.onKeyUp)
    window.removeEventListener('blur', this.onBlur)
    v.removeEventListener('resize', this.invalidateGeometry)
    window.removeEventListener('resize', this.invalidateGeometry)
    this.resizeObserver?.disconnect()
    this.resizeObserver = null
    this.pendingMove = null
    this.rafPending = false
    this.moveSentThisFrame = false
    this.geoValid = false
    this.resetTouchState()
  }

  private invalidateGeometry = () => {
    this.geoValid = false
  }

  private refreshGeometry(): boolean {
    const v = this.opts.video
    const rect = v.getBoundingClientRect()
    const vw = v.videoWidth
    const vh = v.videoHeight
    if (rect.width <= 0 || rect.height <= 0 || vw <= 0 || vh <= 0) {
      this.geoValid = false
      return false
    }
    const scale = Math.min(rect.width / vw, rect.height / vh)
    this.cachedRect = rect
    this.cachedDispW = vw * scale
    this.cachedDispH = vh * scale
    this.cachedOffX = (rect.width - this.cachedDispW) / 2
    this.cachedOffY = (rect.height - this.cachedDispH) / 2
    this.geoValid = true
    return true
  }

  private ensureGeometry(): boolean {
    return this.geoValid || this.refreshGeometry()
  }

  private send(fields: Record<string, unknown>) {
    const dc = this.opts.dc
    if (dc.readyState !== 'open') return
    // 仅 protobuf 编码 + TLV 打包;无 await/日志/DOM
    const payload = encodeMessage({
      deviceId: this.opts.deviceId,
      streamId: this.opts.streamId,
      ...fields,
    })
    dc.send(packTlv(payload, this.pktIndex++))
    this.sentMessages++
  }

  // video 为 object-fit: contain,需剔除上下/左右黑边后归一化到 0~1;黑边内返回 null
  private toRatio(e: MouseEvent | WheelEvent): { x: number; y: number } | null {
    return this.toRatioPoint(e.clientX, e.clientY)
  }

  // 相对模式下的当前坐标:虚拟光标位置;绝对模式下取事件实际坐标
  private eventPos(e: MouseEvent | WheelEvent): { x: number; y: number } | null {
    if (this.relativeMode) return { x: this.virtX, y: this.virtY }
    return this.toRatio(e)
  }

  private displaySize(): { dispW: number; dispH: number } | null {
    if (!this.ensureGeometry()) return null
    return { dispW: this.cachedDispW, dispH: this.cachedDispH }
  }

  private toRatioPoint(clientX: number, clientY: number): { x: number; y: number } | null {
    if (!this.ensureGeometry() || !this.cachedRect) return null
    const x = (clientX - this.cachedRect.left - this.cachedOffX) / this.cachedDispW
    const y = (clientY - this.cachedRect.top - this.cachedOffY) / this.cachedDispH
    if (x < 0 || x > 1 || y < 0 || y > 1) return null
    return { x, y }
  }

  // 进入/退出相对模式;进入时虚拟光标回到画面中心
  setRelativeMode(on: boolean) {
    this.relativeMode = on
    if (on) {
      this.virtX = 0.5
      this.virtY = 0.5
    }
  }

  virtualPos(): { x: number; y: number } {
    return { x: this.virtX, y: this.virtY }
  }

  private sendMouse(button: number, pos: { x: number; y: number }, extra?: { data?: number; pressed?: boolean; released?: boolean; deltaX?: number; deltaY?: number }) {
    const mouseEvent = {
      monitorName: this.opts.monitorName,
      xRatio: pos.x,
      yRatio: pos.y,
      button,
      data: extra?.data ?? 0,
      pressed: extra?.pressed ?? false,
      released: extra?.released ?? false,
      deltaX: extra?.deltaX ?? 0,
      deltaY: extra?.deltaY ?? 0,
      timestamp: Date.now(),
    }
    this.lastMouse = mouseEvent
    this.send({
      type: MSG_TYPE_MOUSE_EVENT,
      mouseEvent,
    })
  }

  private flushMove(pos: MovePos) {
    this.sendMouse(
      BTN_MOUSE_MOVE,
      pos,
      pos.dx !== undefined ? { deltaX: pos.dx, deltaY: pos.dy ?? 0 } : undefined,
    )
  }

  private onMouseMove = (e: MouseEvent) => {
    this.domMoveEvents++
    if (this.viewOnly) return
    if (this.relativeMode) {
      // 指针锁定期间 clientX 冻结,改用 movementX/Y 相对位移驱动虚拟光标
      const dims = this.displaySize()
      if (!dims) return
      this.virtX = Math.min(1, Math.max(0, this.virtX + e.movementX / dims.dispW))
      this.virtY = Math.min(1, Math.max(0, this.virtY + e.movementY / dims.dispH))
      this.sendMoveCoalesced({ x: this.virtX, y: this.virtY, dx: e.movementX, dy: e.movementY })
      return
    }
    const pos = this.toRatio(e)
    if (!pos) return
    this.sendMoveCoalesced(pos)
  }

  // 低延迟合并发送(鼠标与单指拖动共用):
  // - 本帧首次移动:立即发送(0 等待,对齐旧 8ms 节流最坏延迟)
  // - 同帧后续移动:只保留最新坐标,rAF 回调补发一次(对齐竞品/显示器刷新)
  // - 发送缓冲积压时丢纯移动,不丢按键/点击
  private sendMoveCoalesced(pos: MovePos) {
    if (this.opts.dc.bufferedAmount > DROP_MOVE_BUFFERED_BYTES) return
    this.pendingMove = pos
    if (!this.moveSentThisFrame) {
      this.moveSentThisFrame = true
      const p = this.pendingMove
      this.pendingMove = null
      this.flushMove(p)
    }
    if (!this.rafPending) {
      this.rafPending = true
      requestAnimationFrame(() => {
        this.rafPending = false
        this.moveSentThisFrame = false
        if (this.pendingMove && !this.viewOnly) {
          if (this.opts.dc.bufferedAmount > DROP_MOVE_BUFFERED_BYTES) {
            this.pendingMove = null
            return
          }
          const p = this.pendingMove
          this.pendingMove = null
          this.flushMove(p)
        }
      })
    }
  }

  private onMouseDown = (e: MouseEvent) => {
    if (this.viewOnly) return
    const flag = DOWN_FLAGS[e.button]
    if (!flag) return
    const pos = this.eventPos(e)
    if (!pos) return
    e.preventDefault()
    this.sendMouse(flag, pos, { pressed: true })
  }

  private onMouseUp = (e: MouseEvent) => {
    if (this.viewOnly) return
    const flag = UP_FLAGS[e.button]
    if (!flag) return
    const pos = this.eventPos(e)
    if (!pos) return
    e.preventDefault()
    this.sendMouse(flag, pos, { released: true })
  }

  private onWheel = (e: WheelEvent) => {
    e.preventDefault()
    if (this.viewOnly) return
    const pos = this.eventPos(e)
    if (!pos) return
    // deltaMode: 0=像素 1=行;Windows 滚轮 1 notch=120=3 行,浏览器 1 行≈40px
    const unit = e.deltaMode === 1 ? 40 : 1
    const delta = e.deltaY !== 0 ? e.deltaY : e.deltaX
    // 方向取反:浏览器 deltaY>0 向下滚,Windows WHEEL_DELTA>0 向上滚
    this.wheelAcc += -delta * unit
    const whole = Math.trunc(this.wheelAcc)
    this.wheelAcc -= whole
    if (whole !== 0) {
      this.sendMouse(BTN_WHEEL, pos, { data: whole })
    }
  }

  private onContextMenu = (e: MouseEvent) => {
    // 右键交给远端,本地不弹菜单
    e.preventDefault()
  }

  // ---------- 触屏手势 ----------
  // 单指拖动=鼠标移动 / 单指 tap=左键 / 单指长按(500ms)=右键 / 双指拖动=滚轮 / 双指 tap=中键
  // 全程 preventDefault 阻止浏览器默认滚动手势;video CSS 需配 touch-action: none

  private resetTouchState() {
    if (this.longPressTimer !== null) {
      window.clearTimeout(this.longPressTimer)
      this.longPressTimer = null
    }
    this.touchMode = 'none'
    this.touchMoved = false
    this.longPressFired = false
    this.twoMoved = false
    this.twoWheelAcc = 0
  }

  // tap/长按共用:在某点完成一次 按下+抬起
  private tapClick(clientX: number, clientY: number, downFlag: number, upFlag: number) {
    const pos = this.toRatioPoint(clientX, clientY)
    if (!pos) return
    this.sendMouse(downFlag, pos, { pressed: true })
    this.sendMouse(upFlag, pos, { released: true })
  }

  private twoFingerMid(t: TouchList): { x: number; y: number } {
    return {
      x: (t[0].clientX + t[1].clientX) / 2,
      y: (t[0].clientY + t[1].clientY) / 2,
    }
  }

  private onTouchStart = (e: TouchEvent) => {
    e.preventDefault()
    if (this.viewOnly) return
    if (e.touches.length === 1 && (this.touchMode === 'none' || this.touchMode === 'rest')) {
      const t = e.touches[0]
      this.touchMode = 'single'
      this.touchStartX = t.clientX
      this.touchStartY = t.clientY
      this.touchMoved = false
      this.longPressFired = false
      // 单指长按 -> 右键
      this.longPressTimer = window.setTimeout(() => {
        this.longPressTimer = null
        if (this.touchMode !== 'single' || this.touchMoved || this.viewOnly) return
        this.longPressFired = true
        this.tapClick(this.touchStartX, this.touchStartY, BTN_RIGHT_DOWN, BTN_RIGHT_UP)
      }, TOUCH_LONG_PRESS_MS)
    } else if (
      e.touches.length === 2 &&
      (this.touchMode === 'single' || this.touchMode === 'none' || this.touchMode === 'rest')
    ) {
      // 第二指落下(或双指同时落下,CDP 注入场景):取消单指 tap/长按,进入双指手势
      if (this.longPressTimer !== null) {
        window.clearTimeout(this.longPressTimer)
        this.longPressTimer = null
      }
      this.touchMode = 'two'
      this.twoStartAt = Date.now()
      this.twoMoved = false
      this.twoWheelAcc = 0
      const mid = this.twoFingerMid(e.touches)
      this.twoLastMidX = mid.x
      this.twoLastMidY = mid.y
    } else if (e.touches.length >= 3) {
      // 三指及以上:忽略本次手势直到全部抬起
      this.resetTouchState()
      this.touchMode = 'rest'
    }
  }

  private onTouchMove = (e: TouchEvent) => {
    e.preventDefault()
    if (this.viewOnly) return
    if (this.touchMode === 'single' && e.touches.length === 1) {
      const t = e.touches[0]
      if (!this.touchMoved) {
        const dx = t.clientX - this.touchStartX
        const dy = t.clientY - this.touchStartY
        if (Math.hypot(dx, dy) <= TOUCH_TAP_MAX_MOVE_PX) return
        // 超过阈值:转为拖动,取消 tap/长按
        this.touchMoved = true
        if (this.longPressTimer !== null) {
          window.clearTimeout(this.longPressTimer)
          this.longPressTimer = null
        }
      }
      if (this.longPressFired) return // 长按已触发右键,后续位移不再移动鼠标
      const pos = this.toRatioPoint(t.clientX, t.clientY)
      if (pos) this.sendMoveCoalesced(pos)
    } else if (this.touchMode === 'two' && e.touches.length >= 2) {
      // 双指拖动 -> 滚轮:跟踪两指中点纵向位移
      const mid = this.twoFingerMid(e.touches)
      const dy = mid.y - this.twoLastMidY
      if (
        Math.abs(mid.x - this.twoLastMidX) > TOUCH_TAP_MAX_MOVE_PX ||
        Math.abs(mid.y - this.twoLastMidY) > TOUCH_TAP_MAX_MOVE_PX
      ) {
        this.twoMoved = true
      }
      this.twoLastMidX = mid.x
      this.twoLastMidY = mid.y
      // 方向对齐 onWheel:浏览器向下滚 deltaY>0 -> data 取反(手指下拖=向下滚)
      this.twoWheelAcc += -dy
      const whole = Math.trunc(this.twoWheelAcc)
      this.twoWheelAcc -= whole
      if (whole !== 0) {
        const pos = this.toRatioPoint(mid.x, mid.y)
        if (pos) this.sendMouse(BTN_WHEEL, pos, { data: whole })
      }
    }
  }

  private onTouchEnd = (e: TouchEvent) => {
    e.preventDefault()
    if (this.touchMode === 'single' && e.touches.length === 0) {
      if (this.longPressTimer !== null) {
        window.clearTimeout(this.longPressTimer)
        this.longPressTimer = null
      }
      // 单指 tap(未拖动、未触发长按)-> 左键
      if (!this.viewOnly && !this.touchMoved && !this.longPressFired) {
        const t = e.changedTouches[0]
        this.tapClick(t.clientX, t.clientY, BTN_LEFT_DOWN, BTN_LEFT_UP)
      }
      this.resetTouchState()
    } else if (this.touchMode === 'two' || this.touchMode === 'two-ending') {
      if (e.touches.length === 0) {
        // 双指 tap(两指均未明显位移且持续时间短)-> 中键
        // 真实设备/CDP 都是先抬一指再抬第二指,经 two-ending 到达这里
        if (!this.viewOnly && !this.twoMoved && Date.now() - this.twoStartAt <= TOUCH_TWO_FINGER_TAP_MAX_MS) {
          const t = e.changedTouches[0]
          this.tapClick(t.clientX, t.clientY, BTN_MIDDLE_DOWN, BTN_MIDDLE_UP)
        }
        this.resetTouchState()
      } else {
        // 双指中抬起一指:进入收尾态,保留 tap 判定信息(等另一指落地)
        if (this.longPressTimer !== null) {
          window.clearTimeout(this.longPressTimer)
          this.longPressTimer = null
        }
        this.touchMode = 'two-ending'
      }
    } else if (this.touchMode === 'rest' && e.touches.length === 0) {
      this.resetTouchState()
    }
  }

  private onTouchCancel = (e: TouchEvent) => {
    e.preventDefault()
    if (e.touches.length === 0) {
      this.resetTouchState()
    } else {
      this.resetTouchState()
      this.touchMode = 'rest'
    }
  }

  private sendKey(e: KeyboardEvent, down: boolean) {
    const vk = VK_MAP[e.code]
    if (vk === undefined) return
    let numLockStatus = -1
    let capsLockStatus = -1
    let statusCheck = LOCK_KEY_DONT_CARE
    if (isNumLockRelated(vk)) {
      numLockStatus = e.getModifierState('NumLock') ? 1 : 0
      statusCheck = LOCK_KEY_CHECK_NUM_LOCK
    } else if (isCapsLockRelated(vk)) {
      capsLockStatus = e.getModifierState('CapsLock') ? 1 : 0
      statusCheck = LOCK_KEY_CHECK_CAPS_LOCK
    }
    this.send({
      type: MSG_TYPE_KEY_EVENT,
      keyEvent: {
        keyCode: vk,
        down,
        numLockStatus,
        capsLockStatus,
        statusCheck,
        timestamp: Date.now(),
      },
    })
  }

  private isFormTarget(e: KeyboardEvent): boolean {
    const t = e.target as HTMLElement | null
    return !!t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (this.viewOnly || this.isFormTarget(e)) return
    e.preventDefault()
    this.sendKey(e, true)
  }

  private onKeyUp = (e: KeyboardEvent) => {
    if (this.viewOnly || this.isFormTarget(e)) return
    e.preventDefault()
    this.sendKey(e, false)
  }

  private onBlur = () => {
    if (this.viewOnly) return
    // 对齐 win_event_replayer.cpp HandleFocusOutEvent:补发修饰键 release
    const modifiers = [0xa2, 0xa3, 0x11, 0xa0, 0xa1, 0x10, 0xa4, 0xa5, 0x12, 0x5b, 0x5c]
    for (const vk of modifiers) {
      this.send({
        type: MSG_TYPE_KEY_EVENT,
        keyEvent: {
          keyCode: vk,
          down: false,
          numLockStatus: -1,
          capsLockStatus: -1,
          statusCheck: LOCK_KEY_DONT_CARE,
          timestamp: Date.now(),
        },
      })
    }
  }
}
