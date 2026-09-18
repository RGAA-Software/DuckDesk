<script setup lang="ts">
/**
 * DotGlobe：轻量 Canvas 2D 点阵地球（无 WebGL 依赖）。
 * - 斐波那契球面均匀采样 + 陆地贴图过滤，只绘制陆地上的点
 * - 自动旋转、拖拽交互、城市标记脉冲、飞线弧光动画
 * - 使用 Pixels 绿色主题，自动适配页面明暗背景
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

let drawingContext: CanvasRenderingContext2D | null = null
let dots: Dot[] = []
let markers: Marker[] = []
let arcs: Arc[] = []
let animationFrameId = 0
let rotationAngle = 0
let dragging = false
let lastPointerX = 0
let canvasSize = 0
let resizeObserver: ResizeObserver | null = null
let animationStartTime = 0
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches

/** 斐波那契球面均匀采样 */
function fibonacciSphere(count: number): Dot[] {
  const result: Dot[] = []
  const golden = Math.PI * (3 - Math.sqrt(5))
  for (let pointIndex = 0; pointIndex < count; pointIndex++) {
    const y = 1 - (pointIndex / (count - 1)) * 2
    const radius = Math.sqrt(1 - y * y)
    const theta = golden * pointIndex
    result.push({ x: Math.cos(theta) * radius, y, z: Math.sin(theta) * radius })
  }
  return result
}

function latLonToXYZ(latitude: number, longitude: number): Dot {
  const latitudeRadians = (latitude * Math.PI) / 180
  const longitudeRadians = (longitude * Math.PI) / 180
  return {
    x: Math.cos(latitudeRadians) * Math.cos(longitudeRadians),
    y: Math.sin(latitudeRadians),
    z: Math.cos(latitudeRadians) * Math.sin(longitudeRadians),
  }
}

/** 用陆地贴图过滤球面点 */
function loadLandDots(): Promise<Dot[]> {
  return new Promise((resolve) => {
    const mapImage = new Image()
    mapImage.onload = () => {
      const samplingCanvas = document.createElement('canvas')
      samplingCanvas.width = mapImage.width
      samplingCanvas.height = mapImage.height
      const samplingContext = samplingCanvas.getContext('2d')!
      samplingContext.drawImage(mapImage, 0, 0)
      const pixelData = samplingContext.getImageData(0, 0, mapImage.width, mapImage.height).data

      const land = fibonacciSphere(DOT_COUNT).filter((point) => {
        const latitude = (Math.asin(point.y) * 180) / Math.PI
        const longitude = (Math.atan2(point.z, point.x) * 180) / Math.PI
        const pixelX = Math.min(mapImage.width - 1, Math.max(0, Math.round(((longitude + 180) / 360) * mapImage.width)))
        const pixelY = Math.min(mapImage.height - 1, Math.max(0, Math.round(((90 - latitude) / 180) * mapImage.height)))
        return pixelData[(pixelY * mapImage.width + pixelX) * 4] > 100
      })
      resolve(land)
    }
    mapImage.onerror = () => resolve(fibonacciSphere(DOT_COUNT)) // 兜底：全球点阵
    mapImage.src = EARTH_MAP_URI
  })
}

function buildScene(landDots: Dot[]) {
  dots = landDots
  markers = CITY_COORDS.map(([latitude, longitude], cityIndex) => ({
    ...latLonToXYZ(latitude, longitude),
    phase: cityIndex * 0.7,
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
  arcs = pairs.map(([from, to], arcIndex) => ({
    from,
    to,
    phase: arcIndex * 0.9,
    speed: 0.35 + (arcIndex % 3) * 0.12,
  }))
}

/** 绕 Y 轴旋转并投影 */
function project(point: Dot, radius: number, centerX: number, centerY: number) {
  const cos = Math.cos(rotationAngle)
  const sin = Math.sin(rotationAngle)
  const x = point.x * cos - point.z * sin
  const z = point.x * sin + point.z * cos
  return { screenX: centerX + x * radius, screenY: centerY - point.y * radius, z }
}

/** 球面插值（用于飞线） */
function slerp(startPoint: Dot, endPoint: Dot, interpolationRatio: number): Dot {
  const dot = Math.max(-1, Math.min(1, startPoint.x * endPoint.x + startPoint.y * endPoint.y + startPoint.z * endPoint.z))
  const omega = Math.acos(dot)
  if (omega < 1e-5) return { ...startPoint }
  const omegaSine = Math.sin(omega)
  const startWeight = Math.sin((1 - interpolationRatio) * omega) / omegaSine
  const endWeight = Math.sin(interpolationRatio * omega) / omegaSine
  return {
    x: startPoint.x * startWeight + endPoint.x * endWeight,
    y: startPoint.y * startWeight + endPoint.y * endWeight,
    z: startPoint.z * startWeight + endPoint.z * endWeight,
  }
}

function draw(now: number) {
  if (!drawingContext || canvasSize === 0) return
  const devicePixelRatio = window.devicePixelRatio || 1
  const canvasWidth = canvasSize
  drawingContext.clearRect(0, 0, canvasWidth * devicePixelRatio, canvasWidth * devicePixelRatio)

  const centerX = (canvasWidth * devicePixelRatio) / 2
  const centerY = (canvasWidth * devicePixelRatio) / 2
  const radius = canvasWidth * devicePixelRatio * 0.42
  const elapsed = (now - animationStartTime) / 1000

  // 陆地采样点
  for (const point of dots) {
    const { screenX, screenY, z } = project(point, radius, centerX, centerY)
    const front = z > 0
    const alpha = front ? 0.25 + z * 0.65 : 0.06
    const size = (front ? 1.1 + z * 0.9 : 0.8) * devicePixelRatio
    drawingContext.beginPath()
    drawingContext.fillStyle = `rgba(0, 154, 89, ${alpha.toFixed(3)})`
    drawingContext.arc(screenX, screenY, size, 0, Math.PI * 2)
    drawingContext.fill()
  }

  // 飞线弧光
  for (const arc of arcs) {
    const startMarker = markers[arc.from]
    const endMarker = markers[arc.to]
    const segments = 42
    const progress = reducedMotion ? 0.5 : ((elapsed * arc.speed + arc.phase) % 1.4) / 1.4

    drawingContext.beginPath()
    let started = false
    for (let segmentIndex = 0; segmentIndex <= segments; segmentIndex++) {
      const interpolationRatio = segmentIndex / segments
      const point = slerp(startMarker, endMarker, interpolationRatio)
      // 弧顶抬高
      const lift = 1 + Math.sin(interpolationRatio * Math.PI) * 0.22
      const { screenX, screenY, z } = project(
        { x: point.x * lift, y: point.y * lift, z: point.z * lift },
        radius,
        centerX,
        centerY,
      )
      if (z < -0.15) {
        started = false
        continue
      }
      if (!started) {
        drawingContext.moveTo(screenX, screenY)
        started = true
      }
      else {
        drawingContext.lineTo(screenX, screenY)
      }
    }
    drawingContext.strokeStyle = 'rgba(0, 154, 89, 0.22)'
    drawingContext.lineWidth = 1.2 * devicePixelRatio
    drawingContext.stroke()

    // 飞线上的移动亮点
    if (progress <= 1) {
      const head = slerp(startMarker, endMarker, progress)
      const lift = 1 + Math.sin(progress * Math.PI) * 0.22
      const { screenX, screenY, z } = project(
        { x: head.x * lift, y: head.y * lift, z: head.z * lift },
        radius,
        centerX,
        centerY,
      )
      if (z > -0.15) {
        const gradient = drawingContext.createRadialGradient(
          screenX,
          screenY,
          0,
          screenX,
          screenY,
          6 * devicePixelRatio,
        )
        gradient.addColorStop(0, 'rgba(140, 238, 192, 0.9)')
        gradient.addColorStop(1, 'rgba(140, 238, 192, 0)')
        drawingContext.beginPath()
        drawingContext.fillStyle = gradient
        drawingContext.arc(screenX, screenY, 6 * devicePixelRatio, 0, Math.PI * 2)
        drawingContext.fill()
      }
    }
  }

  // 城市标记 + 脉冲
  for (const marker of markers) {
    const { screenX, screenY, z } = project(marker, radius, centerX, centerY)
    if (z < 0) continue

    drawingContext.beginPath()
    drawingContext.fillStyle = `rgba(140, 238, 192, ${(0.5 + z * 0.5).toFixed(3)})`
    drawingContext.arc(screenX, screenY, 2.4 * devicePixelRatio, 0, Math.PI * 2)
    drawingContext.fill()

    const pulse = reducedMotion ? 0.5 : (elapsed * 0.6 + marker.phase) % 1
    drawingContext.beginPath()
    drawingContext.strokeStyle = `rgba(140, 238, 192, ${((1 - pulse) * 0.5 * z).toFixed(3)})`
    drawingContext.lineWidth = 1 * devicePixelRatio
    drawingContext.arc(screenX, screenY, (2.4 + pulse * 10) * devicePixelRatio, 0, Math.PI * 2)
    drawingContext.stroke()
  }
}

function animate(now: number) {
  if (!dragging) {
    rotationAngle += 0.004
  }
  draw(now)
  animationFrameId = requestAnimationFrame(animate)
}

function onPointerDown(event: PointerEvent) {
  dragging = true
  lastPointerX = event.clientX
  canvasRef.value?.setPointerCapture(event.pointerId)
}

function onPointerMove(event: PointerEvent) {
  if (!dragging) return
  rotationAngle += (event.clientX - lastPointerX) * 0.005
  lastPointerX = event.clientX
}

function onPointerUp() {
  dragging = false
}

function onResize() {
  const canvas = canvasRef.value
  if (!canvas) return
  const devicePixelRatio = window.devicePixelRatio || 1
  canvasSize = canvas.offsetWidth
  canvas.width = canvasSize * devicePixelRatio
  canvas.height = canvasSize * devicePixelRatio
}

onMounted(async () => {
  const canvas = canvasRef.value
  if (!canvas) return
  drawingContext = canvas.getContext('2d')

  resizeObserver = new ResizeObserver(onResize)
  resizeObserver.observe(canvas)
  onResize()

  buildScene(await loadLandDots())

  if (reducedMotion) {
    rotationAngle = 0.8
    draw(performance.now())
  }
  else {
    animationStartTime = performance.now()
    animationFrameId = requestAnimationFrame(animate)
  }
  canvas.style.opacity = '1'
})

onBeforeUnmount(() => {
  cancelAnimationFrame(animationFrameId)
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
      class="pointer-events-none absolute inset-0 rounded-full bg-[radial-gradient(circle_at_50%_50%,rgba(0,154,89,0.16),transparent_65%)]"
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
