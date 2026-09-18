<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { stats } from '../data/content'

const { t } = useI18n()

/* Hero 像素插画数据：云朵（24px 方块）× 6 行 */
const cloudBlocks = [
  { x: 112, y: 0, width: 96, height: 24 },
  { x: 64, y: 24, width: 192, height: 24 },
  { x: 40, y: 48, width: 240, height: 24 },
  { x: 16, y: 72, width: 288, height: 24 },
  { x: 16, y: 96, width: 288, height: 24 },
  { x: 40, y: 120, width: 240, height: 24 },
]

/* ---------- 终端动态打字机 ---------- */
/* 4 组脚本覆盖四大业务（云渲染 / 云游戏 / 云桌面 / 远程桌面），
   文案保持简短（≤ 21 字符），避免超出终端宽度 */
interface TermLine {
  text: string
  isOutput?: boolean
}

const terminalScripts: { input: string; output: string }[] = [
  { input: '> render 4k --gpu 64', output: 'done in 00:42:17' },
  { input: '> play pixelrun --stream', output: '60fps · 23ms' },
  { input: '> desktop --vcpu 4', output: 'session ready' },
  { input: '> remote --secure', output: 'connected · ok' },
]

const termLines = ref<TermLine[]>([])
const termTyping = ref('')
const displayLines = computed(() => termLines.value.slice(-3))

let timers: number[] = []

function schedule(callback: () => void, delayMilliseconds: number) {
  timers.push(window.setTimeout(callback, delayMilliseconds))
}

function typeLine(line: string, index: number, onComplete: () => void) {
  termTyping.value = line.slice(0, index)
  if (index < line.length) {
    schedule(() => typeLine(line, index + 1, onComplete), 80)
  } else {
    schedule(onComplete, 300)
  }
}

function runTerminal(step: number) {
  if (step >= terminalScripts.length) {
    /* 一轮结束，清屏后重新开始 */
    schedule(() => {
      termLines.value = []
      termTyping.value = ''
      runTerminal(0)
    }, 900)
    return
  }
  const terminalScript = terminalScripts[step]
  typeLine(terminalScript.input, 1, () => {
    termLines.value = [...termLines.value, { text: terminalScript.input }]
    termTyping.value = ''
    schedule(() => {
      termLines.value = [...termLines.value, { text: terminalScript.output, isOutput: true }]
      schedule(() => runTerminal(step + 1), 1000)
    }, 400)
  })
}

onMounted(() => runTerminal(0))
onBeforeUnmount(() => timers.forEach((timeoutId) => window.clearTimeout(timeoutId)))
</script>

<template>
  <section id="top" class="hero px-grid-bg">
    <div class="container hero-inner">
      <div class="hero-copy">
        <p class="px-eyebrow">{{ t('hero.eyebrow') }}</p>
        <h1 class="hero-title">{{ t('hero.title') }}<span class="hl">{{ t('hero.titleHl') }}</span></h1>
        <p class="hero-sub">{{ t('hero.sub') }}<span class="hl">{{ t('hero.subHl') }}</span></p>
        <p class="hero-desc">{{ t('hero.desc') }}</p>
        <div class="hero-actions">
          <a-button type="primary" size="large" class="px-shadow-btn" href="#services">
            {{ t('hero.btnServices') }}
          </a-button>
          <a-button size="large" class="hero-btn-ghost" href="#contact">{{ t('hero.btnContact') }}</a-button>
        </div>
        <div class="hero-stats">
          <div v-for="stat in stats" :key="stat.labelKey" class="stat">
            <b class="px-mono">{{ stat.value }}</b>
            <span>{{ t(`stats.${stat.labelKey}`) }}</span>
          </div>
        </div>
      </div>

      <div class="hero-art">
        <svg viewBox="0 0 320 340" role="img" aria-label="PIXELS 渲染插画">
          <!-- 像素云朵 -->
          <g fill="#00b96b">
            <rect
              v-for="(cloudBlock, blockIndex) in cloudBlocks"
              :key="blockIndex"
              :x="cloudBlock.x"
              :y="cloudBlock.y"
              :width="cloudBlock.width"
              :height="cloudBlock.height"
            />
          </g>
          <rect x="40" y="120" width="240" height="24" fill="#00a05c" />

          <!-- 渲染终端 -->
          <rect
            x="24"
            y="168"
            width="272"
            height="152"
            :fill="'var(--px-terminal-bg)'"
            :stroke="'var(--px-terminal-stroke)'"
            stroke-width="1"
          />
          <rect x="24" y="168" width="272" height="30" fill="rgba(0,185,107,.16)" />
          <circle cx="48" cy="183" r="4.5" fill="#00b96b" />
          <circle cx="66" cy="183" r="4.5" fill="#2fd88f" opacity=".85" />
          <circle cx="84" cy="183" r="4.5" fill="#b7e8d0" opacity=".6" />
          <text
            x="280"
            y="188"
            text-anchor="end"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="10"
            fill="#a8e8c8"
          >
            px-render · PIXELS
          </text>
          <!-- 终端动态输出（打字机效果） -->
          <text
            v-for="(terminalLine, lineIndex) in displayLines"
            :key="`${lineIndex}-${terminalLine.text}`"
            x="40"
            :y="220 + lineIndex * 22"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="13"
            :fill="terminalLine.isOutput ? '#8cf0be' : '#eafcf2'"
          >
            {{ terminalLine.text }}
          </text>
          <text
            x="40"
            :y="220 + displayLines.length * 22"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="13"
            fill="#eafcf2"
          >
            {{ termTyping }}<tspan class="blink" fill="#00b96b">▌</tspan>
          </text>

          <!-- 漂浮像素装饰 -->
          <rect class="float-a" x="8" y="148" width="10" height="10" fill="#00b96b" opacity=".45" />
          <rect class="float-b" x="300" y="52" width="14" height="14" fill="#00b96b" opacity=".3" />
          <rect x="296" y="140" width="7" height="7" fill="#009a59" opacity=".5" />
          <rect class="float-b" x="12" y="252" width="8" height="8" fill="#00b96b" opacity=".35" />
          <rect x="56" y="12" width="6" height="6" fill="#00b96b" opacity=".4" />
        </svg>
      </div>
    </div>

    <div class="container">
      <div class="px-stairs"><i v-for="stepNumber in 7" :key="stepNumber"></i></div>
    </div>
  </section>
</template>

<style scoped>
.hero {
  position: relative;
  overflow: hidden;
  padding: 148px 0 64px;
  background-color: var(--px-hero-bg);
}

.hero-inner {
  display: grid;
  grid-template-columns: 1.05fr 0.95fr;
  gap: 56px;
  align-items: center;
}

.hero-title {
  font-size: 52px;
  font-weight: 800;
  line-height: 1.25;
  letter-spacing: 1px;
}

.hero-title .hl {
  color: var(--px-green);
}

.hero-sub {
  margin-top: 14px;
  font-size: 22px;
  font-weight: 600;
  color: var(--px-ink);
}

.hero-sub .hl {
  color: var(--px-green);
}

.hero-desc {
  margin-top: 18px;
  max-width: 520px;
  color: var(--px-gray);
  font-size: 15px;
  line-height: 2;
}

.hero-actions {
  display: flex;
  gap: 14px;
  margin-top: 30px;
}

.hero-btn-ghost {
  border-color: rgba(0, 185, 107, 0.4);
  color: var(--px-green-strong);
}

.hero-stats {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 20px;
  max-width: 560px;
  margin-top: 44px;
}

.stat {
  padding-top: 14px;
  border-top: 2px solid rgba(0, 185, 107, 0.3);
}

.stat b {
  display: block;
  font-size: 26px;
  color: var(--px-green-strong);
}

.stat span {
  display: block;
  margin-top: 4px;
  font-size: 13px;
  color: var(--px-gray);
}

.hero-art {
  display: flex;
  justify-content: center;
  position: relative;
}

.hero-art svg {
  width: 100%;
  max-width: 460px;
  height: auto;
}

.float-a {
  animation: floaty 3.2s ease-in-out infinite;
}

.float-b {
  animation: floaty 3.2s ease-in-out 0.8s infinite;
}

@keyframes floaty {
  0%,
  100% {
    transform: translateY(0);
  }
  50% {
    transform: translateY(-8px);
  }
}

.blink {
  animation: blink 1.1s steps(1) infinite;
}

@keyframes blink {
  50% {
    opacity: 0;
  }
}

.px-stairs {
  margin-top: 56px;
}

@media (max-width: 960px) {
  .hero {
    padding-top: 120px;
  }

  .hero-inner {
    grid-template-columns: 1fr;
    gap: 40px;
  }

  .hero-title {
    font-size: 40px;
  }

  .hero-art svg {
    max-width: 380px;
  }
}

@media (max-width: 640px) {
  .hero-title {
    font-size: 32px;
  }

  .hero-sub {
    font-size: 18px;
  }

  .hero-stats {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
