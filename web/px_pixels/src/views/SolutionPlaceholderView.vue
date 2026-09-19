<script setup lang="ts">
import { computed, ref, type Component } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import {
    IconArrowLeft,
    IconArrowUpRight,
    IconClock,
    IconCube3dSphere,
    IconDeviceDesktop,
    IconDeviceGamepad2,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import pixelsLogo from '@/assets/pixels-logo-45.svg'

type SolutionKey = 'remote' | 'gaming' | 'rendering'

interface SolutionDefinition {
    number: string
    icon: Component
    tone: string
}

const props = defineProps<{
    solutionKey: SolutionKey
}>()

const { t, tm } = useI18n()
const router = useRouter()
const contactVisible = ref(false)

const solutionDefinitions: Record<SolutionKey, SolutionDefinition> = {
    remote: { number: '01', icon: IconDeviceDesktop, tone: 'green' },
    gaming: { number: '02', icon: IconDeviceGamepad2, tone: 'blue' },
    rendering: { number: '03', icon: IconCube3dSphere, tone: 'violet' },
}

const activeSolution = computed(() => solutionDefinitions[props.solutionKey])
const roadmapStages = computed(() => tm('site.solutionPages.stages') as string[])

function returnHome() {
    void router.push({ path: '/main', hash: '#solutions' })
}
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="solution-page" :class="`tone-${activeSolution.tone}`">
    <section class="solution-hero">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <span class="product-number">SOLUTION {{ activeSolution.number }}</span>
          <div class="product-title">
            <span>
              <component :is="activeSolution.icon" :size="24" :stroke-width="1.55" />
            </span>
            {{ t(`site.solutionPages.${solutionKey}.title`) }}
          </div>
          <h1>{{ t(`site.solutionPages.${solutionKey}.headline`) }}</h1>
          <p>{{ t(`site.solutionPages.${solutionKey}.description`) }}</p>
          <div class="hero-actions">
            <button class="button-primary" type="button" @click="contactVisible = true">
              {{ t('site.solutionPages.contact') }}
              <IconArrowUpRight :size="17" :stroke-width="1.9" />
            </button>
            <button class="button-secondary" type="button" @click="returnHome">
              <IconArrowLeft :size="17" :stroke-width="1.9" />
              {{ t('site.solutionPages.back') }}
            </button>
          </div>
        </div>

        <div class="solution-visual" aria-hidden="true">
          <div class="visual-orbit orbit-one" />
          <div class="visual-orbit orbit-two" />
          <div class="visual-core">
            <img :src="pixelsLogo" alt="">
            <component :is="activeSolution.icon" :size="42" :stroke-width="1.25" />
          </div>
          <span class="visual-node node-one" />
          <span class="visual-node node-two" />
          <span class="visual-node node-three" />
          <span class="visual-label">PIXELS / {{ activeSolution.number }}</span>
        </div>
      </div>
    </section>

    <section class="page-shell roadmap-section">
      <div class="roadmap-copy">
        <span><IconClock :size="16" :stroke-width="1.8" />{{ t('site.solutionPages.status') }}</span>
        <h2>{{ t('site.solutionPages.roadmapTitle') }}</h2>
        <p>{{ t('site.solutionPages.roadmapDescription') }}</p>
      </div>
      <div class="roadmap-grid">
        <article v-for="(roadmapStage, stageIndex) in roadmapStages" :key="roadmapStage">
          <span>0{{ stageIndex + 1 }}</span>
          <strong>{{ roadmapStage }}</strong>
          <i />
        </article>
      </div>
    </section>
  </div>
</template>

<style scoped>
.solution-page {
    --background: #fafafa;
    --foreground: #18181b;
    --card: #ffffff;
    --primary: #007f49;
    --primary-strong: #006b3d;
    --primary-foreground: #ffffff;
    --secondary: #f4f4f5;
    --muted-foreground: #71717a;
    --border: #e4e4e7;
    --tone: #009a59;
    --tone-soft: #ecfdf5;
    overflow: hidden;
    background: var(--background);
    color: var(--foreground);
}

.tone-blue {
    --tone: #2563eb;
    --tone-soft: #eff6ff;
}

.tone-violet {
    --tone: #7c3aed;
    --tone-soft: #f5f3ff;
}

.page-shell {
    width: min(1200px, calc(100% - 48px));
    margin: 0 auto;
}

.solution-hero {
    position: relative;
    min-height: 690px;
    overflow: hidden;
    border-bottom: 1px solid var(--border);
    background:
        radial-gradient(circle at 78% 35%, color-mix(in srgb, var(--tone) 16%, transparent), transparent 31%),
        linear-gradient(180deg, var(--background), color-mix(in srgb, var(--tone-soft) 50%, var(--background)));
}

.hero-grid {
    position: absolute;
    inset: 0;
    background-image:
        linear-gradient(rgba(0, 127, 73, 0.045) 1px, transparent 1px),
        linear-gradient(90deg, rgba(0, 127, 73, 0.045) 1px, transparent 1px);
    background-size: 38px 38px;
    mask-image: linear-gradient(90deg, transparent, black 30%, black 85%, transparent);
}

.hero-glow {
    position: absolute;
    top: 18%;
    right: 12%;
    width: 430px;
    height: 430px;
    border-radius: 50%;
    background: color-mix(in srgb, var(--tone) 12%, transparent);
    filter: blur(70px);
}

.hero-layout {
    position: relative;
    display: grid;
    min-height: 690px;
    grid-template-columns: 1fr 0.9fr;
    align-items: center;
    gap: 80px;
}

.hero-copy {
    position: relative;
    z-index: 2;
}

.product-number {
    color: var(--tone);
    font: 700 12px var(--font-tech);
    letter-spacing: 0.14em;
}

.product-title {
    display: flex;
    align-items: center;
    gap: 11px;
    margin-top: 24px;
    color: var(--muted-foreground);
    font-size: 14px;
    font-weight: 650;
}

.product-title > span {
    display: grid;
    width: 42px;
    height: 42px;
    place-items: center;
    border-radius: 11px;
    background: var(--tone-soft);
    color: var(--tone);
}

.hero-copy h1 {
    max-width: 680px;
    margin: 24px 0 22px;
    font: 750 clamp(44px, 4.5vw, 68px) / 1.08 var(--font-ui);
    letter-spacing: -0.06em;
}

.hero-copy > p {
    max-width: 620px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 15px;
    line-height: 1.85;
}

.hero-actions {
    display: flex;
    gap: 12px;
    margin-top: 32px;
}

.hero-actions button {
    display: inline-flex;
    min-height: 46px;
    align-items: center;
    justify-content: center;
    gap: 9px;
    padding: 0 19px;
    border-radius: 10px;
    cursor: pointer;
    font: 700 13px var(--font-ui);
}

.button-primary {
    border: 1px solid var(--primary);
    background: var(--primary);
    color: var(--primary-foreground);
}

.button-secondary {
    border: 1px solid var(--border);
    background: var(--card);
    color: var(--foreground);
}

.solution-visual {
    position: relative;
    width: min(100%, 470px);
    aspect-ratio: 1;
    justify-self: end;
}

.visual-orbit {
    position: absolute;
    border: 1px solid color-mix(in srgb, var(--tone) 24%, transparent);
    border-radius: 50%;
}

.orbit-one {
    inset: 12%;
    animation: orbit-spin 22s linear infinite;
}

.orbit-two {
    inset: 24%;
    border-style: dashed;
    animation: orbit-spin 16s linear infinite reverse;
}

.visual-core {
    position: absolute;
    top: 50%;
    left: 50%;
    display: grid;
    width: 150px;
    height: 150px;
    place-items: center;
    border: 1px solid color-mix(in srgb, var(--tone) 24%, var(--border));
    border-radius: 32px;
    background: color-mix(in srgb, var(--card) 92%, transparent);
    box-shadow: 0 28px 60px rgba(15, 23, 42, 0.14);
    color: var(--tone);
    transform: translate(-50%, -50%) rotate(45deg);
    backdrop-filter: blur(18px);
}

.visual-core img,
.visual-core svg {
    position: absolute;
    transform: rotate(-45deg);
}

.visual-core img {
    top: 25px;
    width: 32px;
    height: 32px;
}

.visual-core svg {
    bottom: 24px;
}

.visual-node {
    position: absolute;
    width: 15px;
    height: 15px;
    border: 4px solid var(--card);
    border-radius: 50%;
    background: var(--tone);
    box-shadow: 0 8px 20px color-mix(in srgb, var(--tone) 28%, transparent);
}

.node-one {
    top: 17%;
    left: 25%;
}

.node-two {
    top: 42%;
    right: 10%;
}

.node-three {
    bottom: 14%;
    left: 32%;
}

.visual-label {
    position: absolute;
    right: 8%;
    bottom: 7%;
    color: var(--muted-foreground);
    font: 700 10px var(--font-tech);
    letter-spacing: 0.12em;
}

.roadmap-section {
    display: grid;
    grid-template-columns: 0.85fr 1.15fr;
    align-items: center;
    gap: 80px;
    padding-block: 120px;
}

.roadmap-copy > span {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    color: var(--tone);
    font-size: 12px;
    font-weight: 700;
}

.roadmap-copy h2 {
    margin: 20px 0 16px;
    font: 730 clamp(34px, 3.5vw, 48px) / 1.15 var(--font-ui);
    letter-spacing: -0.05em;
}

.roadmap-copy p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 14px;
    line-height: 1.8;
}

.roadmap-grid {
    display: grid;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 18px;
    background: var(--border);
    gap: 1px;
}

.roadmap-grid article {
    position: relative;
    display: grid;
    min-height: 96px;
    grid-template-columns: 38px 1fr 80px;
    align-items: center;
    padding: 0 28px;
    background: var(--card);
}

.roadmap-grid article span {
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.roadmap-grid article strong {
    font-size: 17px;
    font-weight: 720;
}

.roadmap-grid article i {
    height: 2px;
    border-radius: 2px;
    background: linear-gradient(90deg, var(--tone), transparent);
}

@keyframes orbit-spin {
    to {
        transform: rotate(360deg);
    }
}

:global(html[data-theme='dark'] .solution-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --secondary: #18181b;
    --muted-foreground: #a1a1aa;
    --border: #27272a;
    --tone-soft: #0a3021;
}

:global(html[data-theme='dark'] .solution-page.tone-blue) {
    --tone-soft: #10233e;
}

:global(html[data-theme='dark'] .solution-page.tone-violet) {
    --tone-soft: #271d40;
}

@media (prefers-reduced-motion: reduce) {
    .visual-orbit {
        animation: none;
    }
}

@media (max-width: 820px) {
    .page-shell {
        width: min(100% - 32px, 680px);
    }

    .solution-hero,
    .hero-layout {
        min-height: auto;
    }

    .hero-layout {
        grid-template-columns: 1fr;
        gap: 42px;
        padding-block: 78px 56px;
    }

    .solution-visual {
        width: min(100%, 420px);
        justify-self: center;
    }

    .roadmap-section {
        grid-template-columns: 1fr;
        gap: 44px;
        padding-block: 84px;
    }
}

@media (max-width: 540px) {
    .hero-copy h1 {
        font-size: 42px;
    }

    .hero-actions {
        align-items: stretch;
        flex-direction: column;
    }

    .solution-visual {
        width: 106%;
        margin-left: -3%;
    }

    .visual-core {
        width: 118px;
        height: 118px;
        border-radius: 26px;
    }

    .visual-core img {
        top: 18px;
        width: 27px;
        height: 27px;
    }

    .visual-core svg {
        bottom: 18px;
        width: 34px;
    }

    .roadmap-grid article {
        grid-template-columns: 32px 1fr 48px;
        padding-inline: 20px;
    }
}
</style>
