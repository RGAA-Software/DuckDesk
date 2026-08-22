// 鼠标/键盘输入采集与发送:经 input_data_channel 发送 NetTlvHeader + px.Message
// 协议对齐 src/px_client/front_render/ct_video_widget.cpp 的 SendMouseEvent/SendKeyEvent
import { packTlv } from './tlv'
import { VK_MAP, isNumLockRelated, isCapsLockRelated } from './vk_map'
import {
  encodeMessage,
  MSG_TYPE_KEY_EVENT,
  MSG_TYPE_MOUSE_EVENT,
  MSG_TYPE_TEXT_INPUT,
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

type MovePos = { x: number; y: number }

export class InputController {
  viewOnly = false
  // 指针锁定(相对鼠标)模式:用 movementX/Y 更新虚拟光标比例坐标后发送。
  // client 只上报 ratio(+按键);相对位移由 server 根据绝对坐标换算。
  // ButtonFlag 无相对移动标志位,故相对模式页内维护虚拟光标再发 ratio。
  relativeMode = false
  // 最近一次发出的鼠标事件字段(CDP 调试用)
  lastMouse: Record<string, unknown> | null = null
  // 诊断计数:DOM 鼠标移动事件总数 / 实际发出的输入消息总数(性能面板算速率)
  domMoveEvents = 0
  sentMessages = 0

  private opts: InputOptions
  private pktIndex = 0n
  private attached = false
  // rAF 合并:同帧内多次 mousemove 只在帧末补发最新 ratio;帧内首次立即发送
  private pendingMove: MovePos | null = null
  private rafPending = false
  private moveSentThisFrame = false
  // 当前按下的鼠标键(0/1/2);按住拖动时不因缓冲积压丢 MOVE
  private buttonsHeld = new Set<number>()
  // 上次已发送的画面内像素位置,用于拖出画面/抬起前补发最新 ratio
  private lastSentPxX: number | null = null
  private lastSentPxY: number | null = null
  // 触屏单指拖动:上一采样点,用于去重
  private lastTouchClientX: number | null = null
  private lastTouchClientY: number | null = null
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
  private textSink: HTMLTextAreaElement | null = null
  private composing = false

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
    // video 默认可聚焦,否则点画面后焦点仍停在侧栏 INPUT,WASD 会被 isFormTarget 丢掉
    if (v.tabIndex < 0) v.tabIndex = 0
    // mousemove/mouseup 挂 window:按住拖出 video 外仍能带上 client delta(避免 video+window 双挂导致重复)
    v.addEventListener('mousedown', this.onMouseDown)
    window.addEventListener('mousemove', this.onMouseMove)
    window.addEventListener('mouseup', this.onMouseUp)
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
    this.createTextSink()
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
    v.removeEventListener('mousedown', this.onMouseDown)
    window.removeEventListener('mousemove', this.onMouseMove)
    window.removeEventListener('mouseup', this.onMouseUp)
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
    this.destroyTextSink()
    this.pendingMove = null
    this.rafPending = false
    this.moveSentThisFrame = false
    this.geoValid = false
    this.buttonsHeld.clear()
    this.lastSentPxX = null
    this.lastSentPxY = null
    this.resetTouchState()
  }

  private invalidateGeometry = () => {
    this.geoValid = false
  }

  private createTextSink() {
    if (this.textSink) return
    const sink = document.createElement('textarea')
    sink.setAttribute('aria-hidden', 'true')
    sink.tabIndex = -1
    Object.assign(sink.style, {
      position: 'fixed',
      left: '-10000px',
      top: '0',
      width: '1px',
      height: '1px',
      opacity: '0',
      pointerEvents: 'none',
    })
    sink.addEventListener('input', this.onTextSinkInput)
    sink.addEventListener('compositionstart', this.onCompositionStart)
    sink.addEventListener('compositionend', this.onCompositionEnd)
    document.body.appendChild(sink)
    this.textSink = sink
  }

  private destroyTextSink() {
    if (!this.textSink) return
    this.textSink.removeEventListener('input', this.onTextSinkInput)
    this.textSink.removeEventListener('compositionstart', this.onCompositionStart)
    this.textSink.removeEventListener('compositionend', this.onCompositionEnd)
    this.textSink.remove()
    this.textSink = null
    this.composing = false
  }

  private focusTextSink() {
    if (!this.viewOnly && this.textSink) this.textSink.focus({ preventScroll: true })
  }

  private sendText(text: string) {
    if (!text || this.viewOnly) return
    const encodedBytes = new TextEncoder().encode(text).byteLength
    if (encodedBytes > 4096) return
    this.send({ type: MSG_TYPE_TEXT_INPUT, textInput: { text } })
  }

  private onCompositionStart = () => { this.composing = true }

  private onCompositionEnd = () => {
    this.composing = false
    // Chrome emits the final `input` event after compositionend. Let the
    // common input handler send it exactly once.
  }

  private onTextSinkInput = () => {
    if (!this.textSink || this.composing) return
    this.sendText(this.textSink.value)
    this.textSink.value = ''
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

  /** Headless/CDP: inject absolute click + key without relying on DOM hit-testing. */
  testSend(opts?: { x?: number; y?: number; keyCode?: string }) {
    const x = opts?.x ?? 0.5
    const y = opts?.y ?? 0.5
    const pos = { x, y }
    this.sendMouse(BTN_MOUSE_MOVE, pos)
    this.sendMouse(BTN_LEFT_DOWN, pos, { pressed: true })
    this.sendMouse(BTN_LEFT_UP, pos, { released: true })
    const code = opts?.keyCode ?? 'KeyW'
    const vk = VK_MAP[code]
    if (vk !== undefined) {
      for (const down of [true, false]) {
        this.send({
          type: MSG_TYPE_KEY_EVENT,
          keyEvent: {
            keyCode: vk,
            down,
            numLockStatus: -1,
            capsLockStatus: -1,
            statusCheck: LOCK_KEY_DONT_CARE,
            timestamp: Date.now(),
          },
        })
      }
    }
    return { ok: true, dc: this.opts.dc.readyState, lastMouse: this.lastMouse, keyVk: vk ?? null }
  }

  private describeButton(button: number): string {
    const parts: string[] = []
    if (button & BTN_MOUSE_MOVE) parts.push('MOVE')
    if (button & BTN_LEFT_DOWN) parts.push('LDOWN')
    if (button & BTN_LEFT_UP) parts.push('LUP')
    if (button & BTN_MIDDLE_DOWN) parts.push('MDOWN')
    if (button & BTN_MIDDLE_UP) parts.push('MUP')
    if (button & BTN_RIGHT_DOWN) parts.push('RDOWN')
    if (button & BTN_RIGHT_UP) parts.push('RUP')
    if (button & BTN_WHEEL) parts.push('WHEEL')
    return `${parts.join('|') || 'NONE'}(${button})`
  }

  private markSentPos(pos: { x: number; y: number }) {
    if (!this.ensureGeometry()) return
    this.lastSentPxX = pos.x * this.cachedDispW
    this.lastSentPxY = pos.y * this.cachedDispH
  }

  private sendMouse(button: number, pos: { x: number; y: number }, extra?: { data?: number; pressed?: boolean; released?: boolean; deltaX?: number; deltaY?: number }) {
    const deltaX = extra?.deltaX ?? 0
    const deltaY = extra?.deltaY ?? 0
    const mouseEvent = {
      monitorName: this.opts.monitorName,
      xRatio: pos.x,
      yRatio: pos.y,
      button,
      data: extra?.data ?? 0,
      pressed: extra?.pressed ?? false,
      released: extra?.released ?? false,
      deltaX,
      deltaY,
      timestamp: Date.now(),
    }
    this.lastMouse = mouseEvent
    const dragging = this.buttonsHeld.size > 0 && button === BTN_MOUSE_MOVE
    if (button !== BTN_MOUSE_MOVE || (extra?.data ?? 0) !== 0 || dragging) {
      this.opts.onLog?.(
        `[InputSend] mouse ${this.describeButton(button)} pressed=${!!extra?.pressed} released=${!!extra?.released} data=${extra?.data ?? 0} ratio=(${pos.x.toFixed(4)},${pos.y.toFixed(4)}) monitor=${this.opts.monitorName} dc=${this.opts.dc.readyState}`,
      )
    }
    this.send({
      type: MSG_TYPE_MOUSE_EVENT,
      mouseEvent,
    })
    if (button === BTN_MOUSE_MOVE || extra?.pressed || extra?.released) {
      this.markSentPos(pos)
    }
  }

  private flushMove(pos: MovePos) {
    // 只发 ratio;相对位移由 server 换算
    this.sendMouse(BTN_MOUSE_MOVE, pos)
  }

  private flushPendingMoveNow() {
    if (!this.pendingMove) return
    const p = this.pendingMove
    this.pendingMove = null
    this.flushMove(p)
  }

  private onMouseMove = (e: MouseEvent) => {
    this.domMoveEvents++
    if (this.viewOnly) return
    // 未按键时仅处理落在 video 上的移动
    if (this.buttonsHeld.size === 0 && e.target !== this.opts.video && !this.opts.video.contains(e.target as Node)) {
      return
    }
    const dx = e.movementX
    const dy = e.movementY
    if (dx === 0 && dy === 0) return

    if (this.relativeMode) {
      // movement 仅用于更新本地虚拟光标 ratio,不作为协议 delta
      const dims = this.displaySize()
      if (!dims) return
      this.virtX = Math.min(1, Math.max(0, this.virtX + dx / dims.dispW))
      this.virtY = Math.min(1, Math.max(0, this.virtY + dy / dims.dispH))
      this.sendMoveCoalesced({ x: this.virtX, y: this.virtY })
      return
    }
    const pos = this.toRatio(e)
    if (!pos) {
      if (this.buttonsHeld.size === 0 || this.lastSentPxX === null || this.lastSentPxY === null) return
      if (!this.ensureGeometry()) return
      const x = Math.min(1, Math.max(0, (this.lastSentPxX + dx) / this.cachedDispW))
      const y = Math.min(1, Math.max(0, (this.lastSentPxY + dy) / this.cachedDispH))
      this.sendMoveCoalesced({ x, y })
      return
    }
    this.sendMoveCoalesced({ x: pos.x, y: pos.y })
  }

  // 低延迟合并发送(鼠标与单指拖动共用):只保留最新 ratio
  // 空闲纯移动可在缓冲积压时丢;按住拖动不丢
  private sendMoveCoalesced(pos: MovePos) {
    const dragging = this.buttonsHeld.size > 0
    if (!dragging && this.opts.dc.bufferedAmount > DROP_MOVE_BUFFERED_BYTES) return
    this.pendingMove = { x: pos.x, y: pos.y }
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
          if (this.buttonsHeld.size === 0 && this.opts.dc.bufferedAmount > DROP_MOVE_BUFFERED_BYTES) {
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
    if (this.viewOnly) {
      this.opts.onLog?.(`[InputSend] drop mousedown button=${e.button}, viewOnly`)
      return
    }
    const flag = DOWN_FLAGS[e.button]
    if (!flag) {
      this.opts.onLog?.(`[InputSend] drop mousedown unmapped button=${e.button}`)
      return
    }
    const pos = this.eventPos(e)
    if (!pos) {
      this.opts.onLog?.(`[InputSend] drop mousedown button=${e.button}, outside video content area`)
      return
    }
    e.preventDefault()
    // 隐藏 textarea 必须保持焦点，Chrome 才会产生 input/composition 事件。
    // 之前先 focus textarea、随后又 focus video，导致文字与输入法通道始终失焦；
    // 物理键仍由 window 的 keydown/keyup 监听器转发。
    this.focusTextSink()
    this.flushPendingMoveNow()
    this.buttonsHeld.add(e.button)
    this.markSentPos(pos)
    this.sendMouse(flag, pos, { pressed: true })
  }

  private onMouseUp = (e: MouseEvent) => {
    if (this.viewOnly) {
      this.opts.onLog?.(`[InputSend] drop mouseup button=${e.button}, viewOnly`)
      return
    }
    const flag = UP_FLAGS[e.button]
    if (!flag) {
      this.opts.onLog?.(`[InputSend] drop mouseup unmapped button=${e.button}`)
      return
    }
    if (!this.buttonsHeld.has(e.button)) return
    const pos = this.eventPos(e) ?? (this.lastSentPxX !== null && this.lastSentPxY !== null && this.ensureGeometry()
      ? { x: this.lastSentPxX / this.cachedDispW, y: this.lastSentPxY / this.cachedDispH }
      : null)
    if (!pos) {
      this.opts.onLog?.(`[InputSend] drop mouseup button=${e.button}, outside video content area`)
      this.buttonsHeld.delete(e.button)
      return
    }
    e.preventDefault()
    this.flushPendingMoveNow()
    // 抬起前补发最新 ratio MOVE,供 server 换算相对位移
    if (this.lastSentPxX !== null && this.lastSentPxY !== null && this.ensureGeometry()) {
      const lastX = this.lastSentPxX / this.cachedDispW
      const lastY = this.lastSentPxY / this.cachedDispH
      if (Math.abs(pos.x - lastX) > 1e-6 || Math.abs(pos.y - lastY) > 1e-6) {
        this.opts.onLog?.(`[InputSend] pre-release move ratio=(${pos.x.toFixed(4)},${pos.y.toFixed(4)})`)
        this.sendMouse(BTN_MOUSE_MOVE, pos)
      }
    }
    this.buttonsHeld.delete(e.button)
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
    this.lastTouchClientX = null
    this.lastTouchClientY = null
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
      this.lastTouchClientX = t.clientX
      this.lastTouchClientY = t.clientY
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
      if (!pos) return
      if (this.lastTouchClientX !== null && this.lastTouchClientY !== null
        && t.clientX === this.lastTouchClientX && t.clientY === this.lastTouchClientY) {
        return
      }
      this.lastTouchClientX = t.clientX
      this.lastTouchClientY = t.clientY
      this.sendMoveCoalesced({ x: pos.x, y: pos.y })
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
    if (vk === undefined) {
      this.opts.onLog?.(`[InputSend] drop key unmapped code=${e.code} down=${down}`)
      return
    }
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
    this.opts.onLog?.(
      `[InputSend] key code=${e.code} vk=0x${vk.toString(16)} down=${down} dc=${this.opts.dc.readyState}`,
    )
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
    // 用 activeElement: key 事件的 target 常是焦点元素;侧栏表单抢焦点时必须丢掉,避免误注入
    const t = (document.activeElement as HTMLElement | null) || (e.target as HTMLElement | null)
    if (!t) return false
    if (t === this.opts.video || this.opts.video.contains(t) || t === this.textSink) return false
    return t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable || t.tagName === 'SELECT'
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (this.viewOnly) return
    if (this.isFormTarget(e)) {
      this.opts.onLog?.(`[InputSend] drop key ${e.code}: focus on form, click video first`)
      return
    }
    const textCommit = e.target === this.textSink &&
      (e.isComposing || (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey))
    if (!textCommit) e.preventDefault()
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
