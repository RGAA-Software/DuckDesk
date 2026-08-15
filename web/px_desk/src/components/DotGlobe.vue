<script setup lang="ts">
/**
 * DotGlobe：轻量 Canvas 2D 点阵地球（无 WebGL 依赖）。
 * - 斐波那契球面均匀采样 + 陆地贴图过滤，只绘制陆地上的点
 * - 自动旋转、拖拽交互、城市标记脉冲、飞线弧光动画
 * - 适配深色科技风（品牌蓝点 + 青色标记）
 */
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { EARTH_MAP_URI } from '@/assets/earth-map'

defineProps<{ class?: string }>()

interface Dot {
  x: number
  y: number
  z: number
}

interface Marker extends Dot {
  phase: number
}

interface Arc {
  from: number
  to: number
  phase: number
  speed: number
}

const canvasRef = ref<HTMLCanvasElement>()

const DOT_COUNT = 1400
// 主要城市（纬度, 经度）
const CITY_COORDS: Array<[number, number]> = [
  [39.9, 116.4], // 北京
  [31.2, 121.5], // 上海
  [1.35, 103.8], // 新加坡
  [35.7, 139.7], // 东京
  [51.5, -0.1], // 伦敦
  [48.9, 2.3], // 巴黎
  [40.7, -74.0], // 纽约
  [34.0, -118.2], // 洛杉矶
  [-33.9, 151.2], // 悉尼
  [55.8, 37.6], // 莫斯科
]

let ctx: CanvasRenderingContext2D | null = null
let dots: Dot[] = []
let markers: Marker[] = []
let arcs: Arc[] = []
let rafId = 0
let phi = 0
let dragging = false
let lastPointerX = 0
let canvasSize = 0
let resizeObserver: ResizeObserver | null = null
let startTime = 0
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches

/** 斐波那契球面均匀采样 */
function fibonacciSphere(count: number): Dot[] {
  const result: Dot[] = []
  const golden = Math.PI * (3 - Math.sqrt(5))
  for (let i = 0; i < count; i++) {
    const y = 1 - (i / (count - 1)) * 2
    const radius = Math.sqrt(1 - y * y)
    const theta = golden * i
    result.push({ x: Math.cos(theta) * radius, y, z: Math.sin(theta) * radius })
  }
  return result
}

function latLonToXYZ(lat: number, lon: number): Dot {
  const la = (lat * Math.PI) / 180
  const lo = (lon * Math.PI) / 180
  return {
    x: Math.cos(la) * Math.cos(lo),
    y: Math.sin(la),
    z: Math.cos(la) * Math.sin(lo),
  }
}

/** 用陆地贴图过滤球面点 */
function loadLandDots(): Promise<Dot[]> {
  return new Promise((resolve) => {
    const img = new Image()
    img.onload = () => {
      const c = document.createElement('canvas')
      c.width = img.width
      c.height = img.height
      const c2d = c.getContext('2d')!
      c2d.drawImage(img, 0, 0)
      const data = c2d.getImageData(0, 0, img.width, img.height).data

      const land = fibonacciSphere(DOT_COUNT).filter((p) => {
        const lat = (Math.asin(p.y) * 180) / Math.PI
        const lon = (Math.atan2(p.z, p.x) * 180) / Math.PI
        const u = Math.min(img.width - 1, Math.max(0, Math.round(((lon + 180) / 360) * img.width)))
        const v = Math.min(img.height - 1, Math.max(0, Math.round(((90 - lat) / 180) * img.height)))
        return data[(v * img.width + u) * 4] > 100
      })
      resolve(land)
    }
    img.onerror = () => resolve(fibonacciSphere(DOT_COUNT)) // 兜底：全球点阵
    img.src = EARTH_MAP_URI
  })
}

function buildScene(landDots: Dot[]) {
  dots = landDots
  markers = CITY_COORDS.map(([lat, lon], i) => ({
    ...latLonToXYZ(lat, lon),
    phase: i * 0.7,
  }))
  // 标记点之间随机连几条飞线
  const pairs: Array<[number, number]> = [
    [0, 4],
    [0, 6],
    [2, 8],
    [4, 6],
    [1, 3],
    [6, 7],
    [0, 9],
  ]
  arcs = pairs.map(([from, to], i) => ({
    from,
    to,
    phase: i * 0.9,
    speed: 0.35 + (i % 3) * 0.12,
  }))
}

/** 绕 Y 轴旋转并投影 */
function project(p: Dot, radius: number, cx: number, cy: number) {
  const cos = Math.cos(phi)
  const sin = Math.sin(phi)
  const x = p.x * cos - p.z * sin
  const z = p.x * sin + p.z * cos
  return { sx: cx + x * radius, sy: cy - p.y * radius, z }
}

/** 球面插值（用于飞线） */
function slerp(a: Dot, b: Dot, t: number): Dot {
  const dot = Math.max(-1, Math.min(1, a.x * b.x + a.y * b.y + a.z * b.z))
  const omega = Math.acos(dot)
  if (omega < 1e-5) return { ...a }
  const so = Math.sin(omega)
  const k0 = Math.sin((1 - t) * omega) / so
  const k1 = Math.sin(t * omega) / so
  return {
    x: a.x * k0 + b.x * k1,
    y: a.y * k0 + b.y * k1,
    z: a.z * k0 + b.z * k1,
  }
}

function draw(now: number) {
  if (!ctx || canvasSize === 0) return
  const dpr = window.devicePixelRatio || 1
  const w = canvasSize
  ctx.clearRect(0, 0, w * dpr, w * dpr)

  const cx = (w * dpr) / 2
  const cy = (w * dpr) / 2
  const radius = w * dpr * 0.42
  const elapsed = (now - startTime) / 1000

  // 陆地采样点
  for (const p of dots) {
    const { sx, sy, z } = project(p, radius, cx, cy)
    const front = z > 0
    const alpha = front ? 0.25 + z * 0.65 : 0.06
    const size = (front ? 1.1 + z * 0.9 : 0.8) * dpr
    ctx.beginPath()
    ctx.fillStyle = `rgba(47, 143, 255, ${alpha.toFixed(3)})`
    ctx.arc(sx, sy, size, 0, Math.PI * 2)
    ctx.fill()
  }

  // 飞线弧光
  for (const arc of arcs) {
    const a = markers[arc.from]
    const b = markers[arc.to]
    const segments = 42
    const progress = reducedMotion ? 0.5 : ((elapsed * arc.speed + arc.phase) % 1.4) / 1.4

    ctx.beginPath()
    let started = false
    for (let i = 0; i <= segments; i++) {
      const t = i / segments
      const point = slerp(a, b, t)
      // 弧顶抬高
      const lift = 1 + Math.sin(t * Math.PI) * 0.22
      const { sx, sy, z } = project(
        { x: point.x * lift, y: point.y * lift, z: point.z * lift },
        radius,
        cx,
        cy,
      )
      if (z < -0.15) {
        started = false
        continue
      }
      if (!started) {
        ctx.moveTo(sx, sy)
        started = true
      } else {
        ctx.lineTo(sx, sy)
      }
    }
    ctx.strokeStyle = 'rgba(47, 143, 255, 0.22)'
    ctx.lineWidth = 1.2 * dpr
    ctx.stroke()

    // 飞线上的移动亮点
    if (progress <= 1) {
      const head = slerp(a, b, progress)
      const lift = 1 + Math.sin(progress * Math.PI) * 0.22
      const { sx, sy, z } = project(
        { x: head.x * lift, y: head.y * lift, z: head.z * lift },
        radius,
        cx,
        cy,
      )
      if (z > -0.15) {
        const gradient = ctx.createRadialGradient(sx, sy, 0, sx, sy, 6 * dpr)
        gradient.addColorStop(0, 'rgba(109, 179, 255, 0.9)')
        gradient.addColorStop(1, 'rgba(109, 179, 255, 0)')
        ctx.beginPath()
        ctx.fillStyle = gradient
        ctx.arc(sx, sy, 6 * dpr, 0, Math.PI * 2)
        ctx.fill()
      }
    }
  }

  // 城市标记 + 脉冲
  for (const m of markers) {
    const { sx, sy, z } = project(m, radius, cx, cy)
    if (z < 0) continue

    ctx.beginPath()
    ctx.fillStyle = `rgba(109, 179, 255, ${(0.5 + z * 0.5).toFixed(3)})`
    ctx.arc(sx, sy, 2.4 * dpr, 0, Math.PI * 2)
    ctx.fill()

    const pulse = reducedMotion ? 0.5 : (elapsed * 0.6 + m.phase) % 1
    ctx.beginPath()
    ctx.strokeStyle = `rgba(109, 179, 255, ${((1 - pulse) * 0.5 * z).toFixed(3)})`
    ctx.lineWidth = 1 * dpr
    ctx.arc(sx, sy, (2.4 + pulse * 10) * dpr, 0, Math.PI * 2)
    ctx.stroke()
  }
}

function animate(now: number) {
  if (!dragging) {
    phi += 0.004
  }
  draw(now)
  rafId = requestAnimationFrame(animate)
}

function onPointerDown(e: PointerEvent) {
  dragging = true
  lastPointerX = e.clientX
  canvasRef.value?.setPointerCapture(e.pointerId)
}

function onPointerMove(e: PointerEvent) {
  if (!dragging) return
  phi += (e.clientX - lastPointerX) * 0.005
  lastPointerX = e.clientX
}

function onPointerUp() {
  dragging = false
}

function onResize() {
  const canvas = canvasRef.value
  if (!canvas) return
  const dpr = window.devicePixelRatio || 1
  canvasSize = canvas.offsetWidth
  canvas.width = canvasSize * dpr
  canvas.height = canvasSize * dpr
}

onMounted(async () => {
  const canvas = canvasRef.value
  if (!canvas) return
  ctx = canvas.getContext('2d')

  resizeObserver = new ResizeObserver(onResize)
  resizeObserver.observe(canvas)
  onResize()

  buildScene(await loadLandDots())

  if (reducedMotion) {
    phi = 0.8
    draw(performance.now())
  } else {
    startTime = performance.now()
    rafId = requestAnimationFrame(animate)
  }
  canvas.style.opacity = '1'
})

onBeforeUnmount(() => {
  cancelAnimationFrame(rafId)
  resizeObserver?.disconnect()
})
</script>

<template>
  <div
    class="absolute inset-0 mx-auto aspect-[1/1] w-full max-w-[min(600px,56vw)]"
    :class="[$props.class]"
  >
    <!-- 底部光晕 -->
    <div
      class="pointer-events-none absolute inset-0 rounded-full bg-[radial-gradient(circle_at_50%_50%,rgba(47,143,255,0.16),transparent_65%)]"
    />
    <canvas
      ref="canvasRef"
      class="size-full cursor-grab opacity-0 transition-opacity duration-1000 ease-in-out active:cursor-grabbing"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointerleave="onPointerUp"
    />
  </div>
</template>
