<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { stats } from '../data/content'

const { t } = useI18n()

/* Hero 像素插画数据：云朵（24px 方块）× 6 行 */
const cloudBlocks = [
  { x: 112, y: 0, w: 96, h: 24 },
  { x: 64, y: 24, w: 192, h: 24 },
  { x: 40, y: 48, w: 240, h: 24 },
  { x: 16, y: 72, w: 288, h: 24 },
  { x: 16, y: 96, w: 288, h: 24 },
  { x: 40, y: 120, w: 240, h: 24 },
]
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
          <div v-for="s in stats" :key="s.labelKey" class="stat">
            <b class="px-mono">{{ s.value }}</b>
            <span>{{ t(`stats.${s.labelKey}`) }}</span>
          </div>
        </div>
      </div>

      <div class="hero-art">
        <svg viewBox="0 0 320 340" role="img" aria-label="PIXELS 渲染插画">
          <!-- 像素云朵 -->
          <g fill="#00b96b">
            <rect v-for="(b, i) in cloudBlocks" :key="i" :x="b.x" :y="b.y" :width="b.w" :height="b.h" />
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
          <text
            x="40"
            y="222"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="13"
            fill="#8cf0be"
          >
            &gt; px render --scene demo_4k --gpu 64
          </text>
          <text
            x="40"
            y="244"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="13"
            fill="#8cf0be"
          >
            &gt; streaming 1080p @ 60fps · 23ms
          </text>
          <text
            x="40"
            y="266"
            font-family="'SFMono-Regular', Consolas, monospace"
            font-size="13"
            fill="#8cf0be"
          >
            &gt; done in 00:42:17
          </text>
          <rect class="blink" x="180" y="257" width="9" height="13" fill="#00b96b" />

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
      <div class="px-stairs"><i v-for="n in 7" :key="n"></i></div>
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
