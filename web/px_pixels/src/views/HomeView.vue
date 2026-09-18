<script setup lang="ts">
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import {
    IconArrowDown,
    IconArrowRight,
    IconArrowUpRight,
    IconBolt,
    IconChartHistogram,
    IconCheck,
    IconDevices,
    IconGauge,
    IconPhotoScan,
    IconShieldLock,
} from '@tabler/icons-vue'
import ContactUs from '@/components/ContactUs.vue'
import DotGlobe from '@/components/DotGlobe.vue'
import HeroCloudIllustration from '@/components/HeroCloudIllustration.vue'
import platformManyToManySvg from '@/assets/diagram/platform-many-to-many.svg?raw'
import cloudGamingScene from '@/assets/showcase/cloud-gaming-csgo.webp'
import cloudRenderingScene from '@/assets/showcase/cloud-rendering-blender.webp'
import remoteDesktopScene from '@/assets/showcase/remote-desktop-photoshop.webp'

interface Solution {
    id: string
    number: string
    title: string
    description: string
    points: string[]
}

interface Metric {
    value: string
    label: string
}

interface Capability {
    title: string
    description: string
}

const { t, tm } = useI18n()
const contactVisible = ref(false)
const solutions = computed(() => tm('site.solutions.cards') as Solution[])
const metrics = computed(() => tm('site.metrics') as Metric[])
const capabilities = computed(() => tm('site.capabilities.items') as Capability[])
const platformPoints = computed(() => tm('site.platform.points') as string[])
const journeySteps = computed(() => tm('site.journey.steps') as string[])
const capabilityIcons = [
    IconPhotoScan,
    IconGauge,
    IconShieldLock,
    IconBolt,
    IconDevices,
    IconChartHistogram,
]

function scrollToSolutions() {
    document.querySelector('#solutions')?.scrollIntoView({ behavior: 'smooth' })
}
</script>

<template>
  <ContactUs v-model="contactVisible" />
  <div class="home-page">
    <section class="hero-section">
      <div class="hero-grid" aria-hidden="true" />
      <div class="hero-glow" aria-hidden="true" />
      <DotGlobe class="hero-globe" aria-hidden="true" />

      <div class="page-shell hero-layout">
        <div class="hero-copy">
          <div class="hero-kicker">
            <span class="live-dot" />
            PIXELS CLOUD EXPERIENCE
          </div>
          <h1>
            <span>{{ t('site.hero.titleLead') }}</span>
            <strong>{{ t('site.hero.titleAccent') }}</strong>
          </h1>
          <p>{{ t('site.hero.description') }}</p>
          <div class="hero-actions">
            <button class="button-primary" type="button" @click="contactVisible = true">
              {{ t('site.actions.consult') }}
              <IconArrowUpRight :size="17" :stroke-width="1.9" aria-hidden="true" />
            </button>
            <button class="button-secondary" type="button" @click="scrollToSolutions">
              {{ t('site.actions.explore') }}
              <IconArrowDown :size="17" :stroke-width="1.9" aria-hidden="true" />
            </button>
          </div>
          <div class="hero-trust">
            <span>ANYTIME · ANYWHERE · ALWAYS WITHIN REACH</span>
          </div>
        </div>

        <HeroCloudIllustration class="hero-illustration" />
      </div>
    </section>

    <section class="metrics-section">
      <div class="page-shell metrics-grid">
        <div v-for="metric in metrics" :key="metric.value" class="metric">
          <strong>{{ metric.value }}</strong>
          <span>{{ metric.label }}</span>
        </div>
      </div>
    </section>

    <section id="solutions" class="section page-shell solutions-section">
      <div class="section-heading centered-heading">
        <span>{{ t('site.solutions.eyebrow') }}</span>
        <h2>{{ t('site.solutions.title') }}</h2>
        <p>{{ t('site.solutions.description') }}</p>
      </div>

      <div class="solution-stack">
        <article
          v-for="solution in solutions"
          :key="solution.id"
          class="solution-panel"
          :class="`solution-${solution.id}`"
        >
          <div class="solution-copy">
            <span class="solution-number">FEATURE {{ solution.number }}</span>
            <h3>{{ solution.title }}</h3>
            <p>{{ solution.description }}</p>
            <ul>
              <li v-for="point in solution.points" :key="point">
                <i><IconCheck :size="11" :stroke-width="2.2" /></i>{{ point }}
              </li>
            </ul>
            <button type="button" @click="contactVisible = true">
              {{ t('site.actions.learnMore') }}
              <IconArrowRight :size="17" :stroke-width="1.9" aria-hidden="true" />
            </button>
          </div>

          <div class="solution-visual" aria-hidden="true">
            <div class="scene-stage" :class="`scene-${solution.id}`">
              <div class="scene-screen">
                <div class="scene-screen-bar">
                  <span class="scene-window-controls"><i /><i /><i /></span>
                  <small v-if="solution.id === 'remote'">PIXELS / DESKTOP</small>
                  <small v-else-if="solution.id === 'game'">PIXELS / GAME</small>
                  <small v-else>PIXELS / RENDER</small>
                  <b><i />{{ t('site.hero.live') }}</b>
                </div>
                <div class="scene-media">
                  <img
                    v-if="solution.id === 'remote'"
                    :src="remoteDesktopScene"
                    alt=""
                  >
                  <img
                    v-else-if="solution.id === 'game'"
                    :src="cloudGamingScene"
                    alt=""
                  >
                  <img
                    v-else
                    :src="cloudRenderingScene"
                    alt=""
                  >
                </div>
              </div>

              <div v-if="solution.id === 'remote'" class="remote-device">
                <span />
                <img :src="remoteDesktopScene" alt="">
                <i />
              </div>

            </div>
          </div>
        </article>
      </div>
    </section>

    <section id="capabilities" class="capabilities-section">
      <div class="page-shell capabilities-layout">
        <div class="section-heading capabilities-heading">
          <span>{{ t('site.capabilities.eyebrow') }}</span>
          <h2>{{ t('site.capabilities.title') }}</h2>
          <p>{{ t('site.capabilities.description') }}</p>
        </div>
        <div class="capability-grid">
          <article
            v-for="(capability, capabilityIndex) in capabilities"
            :key="capability.title"
          >
            <div class="capability-icon">
              <component
                :is="capabilityIcons[capabilityIndex]"
                :size="21"
                :stroke-width="1.7"
              />
            </div>
            <span>0{{ capabilityIndex + 1 }}</span>
            <h3>{{ capability.title }}</h3>
            <p>{{ capability.description }}</p>
          </article>
        </div>
      </div>
    </section>

    <section id="platform" class="section platform-section">
      <div class="page-shell platform-layout">
        <div class="platform-copy">
          <div class="section-heading">
            <span>{{ t('site.platform.eyebrow') }}</span>
            <h2>{{ t('site.platform.title') }}</h2>
            <p>{{ t('site.platform.description') }}</p>
          </div>
          <div class="platform-points">
            <div v-for="point in platformPoints" :key="point">
              <i><IconCheck :size="11" :stroke-width="2.2" /></i><span>{{ point }}</span>
            </div>
          </div>
        </div>

        <div class="platform-mesh" aria-hidden="true" v-html="platformManyToManySvg" />
      </div>
    </section>

    <section class="section journey-section">
      <div class="page-shell">
        <div class="section-heading journey-heading">
          <span>{{ t('site.journey.eyebrow') }}</span>
          <h2>{{ t('site.journey.title') }}</h2>
        </div>
        <ol class="journey-list">
          <li v-for="(step, stepIndex) in journeySteps" :key="step">
            <span>0{{ stepIndex + 1 }}</span>
            <strong>{{ step }}</strong>
            <IconArrowRight
              v-if="stepIndex < journeySteps.length - 1"
              class="journey-arrow"
              :size="28"
              :stroke-width="1.3"
            />
          </li>
        </ol>
      </div>
    </section>

    <section class="page-shell final-cta">
      <div class="cta-grid" aria-hidden="true" />
      <div class="cta-orb" aria-hidden="true" />
      <div class="cta-copy">
        <span>{{ t('site.cta.eyebrow') }}</span>
        <h2>{{ t('site.cta.title') }}</h2>
        <p>{{ t('site.cta.description') }}</p>
      </div>
      <button type="button" @click="contactVisible = true">
        {{ t('site.actions.start') }}
        <IconArrowUpRight :size="18" :stroke-width="1.9" aria-hidden="true" />
      </button>
    </section>
  </div>
</template>

<style scoped>
.home-page {
    --background: #fafafa;
    --foreground: #18181b;
    --card: #ffffff;
    --primary: #007f49;
    --primary-strong: #006b3d;
    --primary-bright: #009a59;
    --primary-foreground: #ffffff;
    --secondary: #f4f4f5;
    --secondary-foreground: #27272a;
    --muted-foreground: #71717a;
    --accent: #ecfdf5;
    --accent-foreground: #006b3d;
    --border: #e4e4e7;
    overflow: hidden;
    background: var(--background);
    color: var(--foreground);
}

.page-shell {
    width: min(1200px, calc(100% - 48px));
    margin: 0 auto;
}

.hero-section {
    position: relative;
    min-height: 700px;
    overflow: hidden;
    border-bottom: 1px solid var(--border);
    background:
        radial-gradient(circle at 78% 32%, rgba(0, 154, 89, 0.12), transparent 31%),
        linear-gradient(180deg, var(--background), color-mix(in srgb, var(--accent) 46%, var(--background)));
}

.hero-grid,
.cta-grid {
    position: absolute;
    inset: 0;
    background-image:
        linear-gradient(to right, rgba(0, 127, 73, 0.055) 1px, transparent 1px),
        linear-gradient(to bottom, rgba(0, 127, 73, 0.055) 1px, transparent 1px);
    background-size: 42px 42px;
    mask-image: linear-gradient(90deg, transparent, black 50%, transparent);
}

.hero-glow {
    position: absolute;
    top: 120px;
    right: 5%;
    width: 480px;
    height: 480px;
    border-radius: 50%;
    background: rgba(0, 154, 89, 0.08);
    filter: blur(80px);
}

.hero-globe {
    z-index: 1;
    right: -4%;
    left: auto !important;
    top: 54px;
    width: min(650px, 52vw) !important;
    max-width: none !important;
    opacity: 0.8;
}

.hero-layout {
    position: relative;
    z-index: 2;
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(480px, 0.94fr);
    align-items: center;
    gap: 76px;
    min-height: 700px;
    padding: 70px 0 78px;
}

.hero-copy {
    position: relative;
    z-index: 3;
}

.hero-kicker,
.section-heading > span,
.cta-copy > span {
    display: flex;
    align-items: center;
    gap: 9px;
    color: var(--primary);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.13em;
    text-transform: uppercase;
}

.live-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--primary-bright);
    box-shadow: 0 0 0 5px rgba(0, 154, 89, 0.12);
}

.hero-copy h1 {
    margin: 24px 0 22px;
    font: 750 clamp(52px, 5.2vw, 78px) / 1.04 var(--font-ui);
    letter-spacing: -0.065em;
}

.hero-copy h1 span,
.hero-copy h1 strong {
    display: block;
}

.hero-copy h1 strong {
    color: var(--primary);
    font-weight: 750;
}

.hero-copy > p {
    max-width: 590px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 16px;
    line-height: 1.85;
}

.hero-actions {
    display: flex;
    gap: 12px;
    margin-top: 34px;
}

.button-primary,
.button-secondary,
.final-cta > button {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 11px;
    min-height: 48px;
    padding: 0 19px;
    border-radius: 11px !important;
    cursor: pointer;
    font: 700 14px var(--font-ui);
    transition: transform 160ms ease, box-shadow 160ms ease;
}

.button-primary {
    border: 1px solid var(--primary);
    background: var(--primary);
    box-shadow: 0 14px 32px rgba(0, 127, 73, 0.2);
    color: var(--primary-foreground);
}

.button-secondary {
    border: 1px solid var(--border);
    background: var(--card);
    color: var(--foreground);
}

.button-primary:hover,
.button-secondary:hover,
.final-cta > button:hover {
    transform: translateY(-2px);
}

.hero-trust {
    margin-top: 36px;
}

.hero-trust span {
    color: var(--muted-foreground);
    font: 700 11px var(--font-tech);
    letter-spacing: 0.12em;
}

.hero-illustration {
    position: relative;
    z-index: 2;
    width: 108%;
    max-width: none;
    margin-left: -4%;
}

.metrics-section {
    border-bottom: 1px solid var(--border);
    background: var(--card);
}

.metrics-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
}

.metric {
    display: grid;
    grid-template-columns: auto 1fr;
    align-items: center;
    gap: 17px;
    min-height: 112px;
    padding: 24px 44px;
    border-right: 1px solid var(--border);
}

.metric:first-child {
    padding-left: 0;
}

.metric:last-child {
    border-right: 0;
}

.metric strong {
    color: var(--primary);
    font-size: 22px;
    white-space: nowrap;
}

.metric span {
    color: var(--muted-foreground);
    font-size: 12px;
    line-height: 1.55;
}

.section {
    padding: 120px 0;
}

.section-heading h2 {
    max-width: 720px;
    margin: 17px 0 18px;
    color: var(--foreground);
    font: 730 clamp(34px, 4vw, 52px) / 1.16 var(--font-ui);
    letter-spacing: -0.052em;
}

.section-heading p {
    max-width: 650px;
    margin: 0;
    color: var(--muted-foreground);
    font-size: 15px;
    line-height: 1.8;
}

.centered-heading {
    display: flex;
    align-items: center;
    flex-direction: column;
    text-align: center;
}

.solution-stack {
    display: grid;
    gap: 22px;
    margin-top: 58px;
}

.solution-panel {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    min-height: 430px;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 22px !important;
    background: var(--card);
    box-shadow: 0 14px 45px rgba(24, 24, 27, 0.045);
}

.solution-panel:nth-child(even) .solution-copy {
    order: 2;
}

.solution-copy {
    display: flex;
    align-items: flex-start;
    justify-content: center;
    flex-direction: column;
    padding: 54px 60px;
}

.solution-number {
    color: var(--primary);
    font-family: "10 Pixel", sans-serif;
    font-size: 14px;
    font-weight: 700;
    letter-spacing: 0.08em;
}

.solution-copy h3 {
    margin: 19px 0 15px;
    font-size: 32px;
    letter-spacing: -0.035em;
}

.solution-copy > p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 14px;
    line-height: 1.78;
}

.solution-copy ul {
    display: grid;
    gap: 10px;
    margin: 25px 0 28px;
    padding: 0;
    list-style: none;
}

.solution-copy li {
    display: flex;
    align-items: center;
    gap: 9px;
    color: var(--secondary-foreground);
    font-size: 13px;
}

.solution-copy li i {
    display: grid;
    width: 18px;
    height: 18px;
    place-items: center;
    border-radius: 50%;
    background: var(--accent);
    color: var(--primary);
    font-size: 9px;
    font-style: normal;
}

.solution-copy button {
    display: inline-flex;
    align-items: center;
    gap: 9px;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--primary);
    cursor: pointer;
    font: 700 13px var(--font-ui);
}

.solution-visual {
    position: relative;
    min-height: 430px;
    overflow: hidden;
    isolation: isolate;
    background: var(--secondary);
}

.solution-visual::before {
    position: absolute;
    inset: 0;
    z-index: -1;
    background-image:
        linear-gradient(rgba(0, 127, 73, 0.06) 1px, transparent 1px),
        linear-gradient(90deg, rgba(0, 127, 73, 0.06) 1px, transparent 1px);
    background-size: 30px 30px;
    content: '';
    mask-image: linear-gradient(135deg, transparent 2%, black 24%, black 72%, transparent 96%);
}

.solution-visual::after {
    position: absolute;
    z-index: -1;
    right: 4%;
    bottom: -31%;
    left: 4%;
    height: 58%;
    border-radius: 50%;
    background: linear-gradient(
        90deg,
        rgba(0, 154, 89, 0.36),
        rgba(14, 165, 233, 0.26),
        rgba(168, 85, 247, 0.26)
    );
    content: '';
    filter: blur(48px);
}

.scene-stage {
    position: absolute;
    inset: 0;
    transition: transform 320ms cubic-bezier(0.2, 0.8, 0.2, 1);
}

.solution-panel:hover .scene-stage {
    transform: translateY(-5px);
}

.scene-screen {
    position: absolute;
    top: 16%;
    right: 7%;
    bottom: 16%;
    left: 8%;
    overflow: hidden;
    border: 1px solid rgba(255, 255, 255, 0.72);
    border-radius: 18px;
    background: #101820;
    box-shadow:
        0 28px 54px rgba(15, 23, 42, 0.23),
        0 0 0 1px rgba(15, 23, 42, 0.08);
}

.scene-screen-bar {
    display: grid;
    grid-template-columns: 1fr auto 1fr;
    align-items: center;
    height: 36px;
    padding: 0 13px;
    background: rgba(10, 16, 22, 0.96);
    color: rgba(255, 255, 255, 0.66);
}

.scene-screen-bar small {
    font: 700 9px var(--font-tech);
    letter-spacing: 0.12em;
}

.scene-screen-bar b {
    display: inline-flex;
    align-items: center;
    justify-self: end;
    gap: 6px;
    color: #8ceec0;
    font: 700 9px var(--font-tech);
    letter-spacing: 0.08em;
}

.scene-screen-bar b i {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #22c55e;
    box-shadow: 0 0 0 4px rgba(34, 197, 94, 0.12);
}

.scene-window-controls {
    display: flex;
    gap: 5px;
}

.scene-window-controls i {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.24);
}

.scene-window-controls i:first-child {
    background: #fb7185;
}

.scene-window-controls i:nth-child(2) {
    background: #fbbf24;
}

.scene-window-controls i:last-child {
    background: #34d399;
}

.scene-media {
    position: absolute;
    inset: 36px 0 0;
    overflow: hidden;
}

.scene-media::after {
    position: absolute;
    inset: 0;
    background: linear-gradient(180deg, transparent 60%, rgba(0, 0, 0, 0.28));
    content: '';
    pointer-events: none;
}

.scene-media img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    transition: transform 700ms cubic-bezier(0.2, 0.8, 0.2, 1);
}

.solution-panel:hover .scene-media img {
    transform: scale(1.025);
}

.scene-remote .scene-media img {
    object-position: 52% 48%;
}

.scene-game .scene-media img {
    object-position: 50% 50%;
}

.scene-render .scene-media img {
    object-position: 62% 50%;
}

.remote-device {
    position: absolute;
    z-index: 2;
    bottom: 7%;
    left: 4%;
    width: 154px;
    height: 82px;
    overflow: hidden;
    border: 5px solid #111827;
    border-radius: 17px;
    background: #111827;
    box-shadow: 0 22px 36px rgba(15, 23, 42, 0.28);
}

.remote-device > span {
    position: absolute;
    z-index: 2;
    top: 50%;
    right: 3px;
    left: auto;
    width: 3px;
    height: 18px;
    border-radius: 2px;
    background: rgba(255, 255, 255, 0.35);
    transform: translateY(-50%);
}

.remote-device img {
    width: 100%;
    height: 100%;
    border-radius: 12px;
    object-fit: cover;
    object-position: 52% center;
}

.remote-device > i {
    position: absolute;
    z-index: 2;
    right: 5px;
    bottom: 5px;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: #22c55e;
    box-shadow: 0 0 0 4px rgba(34, 197, 94, 0.18);
}

.solution-remote .solution-visual {
    background:
        radial-gradient(circle at 18% 18%, rgba(0, 154, 89, 0.22), transparent 31%),
        linear-gradient(145deg, #f0fdf4, #dff7ed 54%, #dbeafe);
}

.solution-game .solution-visual {
    background:
        radial-gradient(circle at 82% 18%, rgba(124, 58, 237, 0.22), transparent 32%),
        linear-gradient(145deg, #ecfdf5, #dbeafe 50%, #ede9fe);
}

.solution-render .solution-visual {
    background:
        radial-gradient(circle at 20% 19%, rgba(249, 115, 22, 0.2), transparent 31%),
        linear-gradient(145deg, #effdf5, #fef3c7 48%, #fce7f3);
}

.capabilities-section {
    padding: 120px 0;
    border-top: 1px solid var(--border);
    border-bottom: 1px solid var(--border);
    background: var(--secondary);
}

.capabilities-layout {
    display: grid;
    grid-template-columns: 0.72fr 1.28fr;
    align-items: start;
    gap: 75px;
}

.capabilities-heading {
    position: sticky;
    top: 130px;
}

.capability-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: 20px !important;
    background: var(--border);
    gap: 1px;
}

.capability-grid article {
    position: relative;
    min-height: 220px;
    padding: 31px;
    background: var(--card);
}

.capability-grid article > span {
    position: absolute;
    top: 31px;
    right: 31px;
    color: var(--muted-foreground);
    font: 10px var(--font-tech);
}

.capability-icon {
    display: grid;
    width: 42px;
    height: 42px;
    place-items: center;
    border-radius: 11px;
    background: var(--accent);
    color: var(--primary);
}

.capability-icon svg {
    width: 21px;
    fill: none;
    stroke: currentColor;
    stroke-linecap: round;
    stroke-linejoin: round;
    stroke-width: 1.6;
}

.capability-grid h3 {
    margin: 25px 0 9px;
    font-size: 17px;
}

.capability-grid p {
    margin: 0;
    color: var(--muted-foreground);
    font-size: 13px;
    line-height: 1.7;
}

.platform-section {
    background: var(--background);
}

.platform-layout {
    display: grid;
    grid-template-columns: 0.88fr 1.12fr;
    align-items: center;
    gap: 78px;
}

.platform-points {
    display: grid;
    gap: 12px;
    margin-top: 30px;
}

.platform-points > div {
    display: flex;
    align-items: center;
    gap: 10px;
    color: var(--secondary-foreground);
    font-size: 13px;
}

.platform-points i {
    display: grid;
    width: 20px;
    height: 20px;
    place-items: center;
    border-radius: 50%;
    background: var(--accent);
    color: var(--primary);
    font-size: 9px;
    font-style: normal;
}

.platform-mesh {
    width: 100%;
    min-width: 0;
}

.journey-section {
    padding-top: 0;
}

.journey-heading {
    max-width: 700px;
}

.journey-list {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    margin: 52px 0 0;
    padding: 0;
    border-top: 1px solid var(--border);
    list-style: none;
}

.journey-list li {
    position: relative;
    min-height: 160px;
    padding: 29px 44px 20px 0;
}

.journey-list li > span {
    display: block;
    margin-bottom: 25px;
    color: var(--primary);
    font: 700 11px var(--font-tech);
}

.journey-list strong {
    display: block;
    max-width: 270px;
    font-size: 16px;
    line-height: 1.55;
}

.journey-arrow {
    position: absolute;
    top: 65px;
    right: 38px;
    color: var(--border);
}

.final-cta {
    position: relative;
    display: flex;
    min-height: 330px;
    align-items: center;
    justify-content: space-between;
    gap: 55px;
    margin-bottom: 120px;
    padding: 62px 68px;
    overflow: hidden;
    border-radius: 24px !important;
    background: linear-gradient(125deg, #052e22, #007f49 70%, #009a59);
    color: #ffffff;
}

.cta-grid {
    opacity: 0.35;
    mask-image: linear-gradient(90deg, transparent, black);
}

.cta-orb {
    position: absolute;
    right: -100px;
    width: 380px;
    height: 380px;
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 50%;
    box-shadow: 0 0 0 48px rgba(255, 255, 255, 0.025);
}

.cta-copy {
    position: relative;
    z-index: 1;
}

.cta-copy > span {
    color: #8ceec0;
}

.cta-copy h2 {
    max-width: 690px;
    margin: 16px 0 14px;
    color: #ffffff;
    font: 730 clamp(32px, 3.5vw, 48px) / 1.16 var(--font-ui);
    letter-spacing: -0.05em;
}

.cta-copy p {
    margin: 0;
    color: rgba(255, 255, 255, 0.68);
    font-size: 14px;
    line-height: 1.7;
}

.final-cta > button {
    position: relative;
    z-index: 1;
    flex: 0 0 auto;
    border: 1px solid #ffffff;
    background: #ffffff;
    color: #006b3d;
}

:global(html[data-theme='dark'] .home-page) {
    --background: #09090b;
    --foreground: #fafafa;
    --card: #101014;
    --primary: #009a59;
    --primary-strong: #007f49;
    --primary-bright: #009a59;
    --primary-foreground: #ffffff;
    --secondary: #18181b;
    --secondary-foreground: #fafafa;
    --muted-foreground: #a1a1aa;
    --accent: #052e22;
    --accent-foreground: #8ceec0;
    --border: #27272a;
}

:global(html[data-theme='dark'] .hero-section) {
    background:
        radial-gradient(circle at 78% 32%, rgba(0, 154, 89, 0.13), transparent 31%),
        linear-gradient(180deg, #09090b, #0b1511);
}

:global(html[data-theme='dark'] .solution-remote .solution-visual) {
    background:
        radial-gradient(circle at 18% 18%, rgba(0, 154, 89, 0.2), transparent 31%),
        linear-gradient(145deg, #081811, #0b1f1a 54%, #101b2a);
}

:global(html[data-theme='dark'] .solution-game .solution-visual) {
    background:
        radial-gradient(circle at 82% 18%, rgba(124, 58, 237, 0.18), transparent 32%),
        linear-gradient(145deg, #081811, #0d1b29 50%, #17132b);
}

:global(html[data-theme='dark'] .solution-render .solution-visual) {
    background:
        radial-gradient(circle at 20% 19%, rgba(249, 115, 22, 0.16), transparent 31%),
        linear-gradient(145deg, #081811, #211b0c 48%, #231221);
}

@media (max-width: 1020px) {
    .hero-layout {
        grid-template-columns: 1fr 0.82fr;
        gap: 35px;
    }

    .hero-illustration {
        width: 115%;
        margin-left: -7.5%;
    }

    .solution-copy {
        padding: 45px 40px;
    }

    .scene-screen {
        right: 5%;
        left: 6%;
    }

    .capabilities-layout,
    .platform-layout {
        gap: 45px;
    }
}

@media (max-width: 820px) {
    .page-shell {
        width: min(100% - 32px, 680px);
    }

    .hero-section,
    .hero-layout {
        min-height: auto;
    }

    .hero-layout {
        grid-template-columns: 1fr;
        padding: 84px 0 66px;
    }

    .hero-copy h1 {
        font-size: clamp(46px, 12vw, 68px);
    }

    .hero-globe {
        top: 275px;
        right: -22%;
        width: 600px !important;
        opacity: 0.56;
    }

    .hero-illustration {
        width: min(100%, 620px);
        margin: 0 auto;
    }

    .metrics-grid {
        grid-template-columns: 1fr;
    }

    .metric,
    .metric:first-child {
        min-height: 84px;
        padding: 18px 0;
        border-right: 0;
        border-bottom: 1px solid var(--border);
    }

    .metric:last-child {
        border-bottom: 0;
    }

    .section,
    .capabilities-section {
        padding: 84px 0;
    }

    .solution-panel {
        grid-template-columns: 1fr;
    }

    .solution-panel:nth-child(even) .solution-copy {
        order: 0;
    }

    .solution-visual {
        min-height: 360px;
    }

    .capabilities-layout,
    .platform-layout {
        grid-template-columns: 1fr;
    }

    .capabilities-heading {
        position: static;
    }

    .journey-section {
        padding-top: 0;
    }

    .journey-list {
        grid-template-columns: 1fr;
    }

    .journey-list li {
        min-height: 110px;
        padding: 24px 0;
        border-bottom: 1px solid var(--border);
    }

    .journey-list li > span {
        margin-bottom: 11px;
    }

    .journey-arrow {
        display: none;
    }

    .final-cta {
        display: block;
        min-height: 0;
        margin-bottom: 84px;
        padding: 48px 36px;
    }

    .final-cta > button {
        margin-top: 28px;
    }
}

@media (max-width: 540px) {
    .hero-layout {
        padding-top: 66px;
    }

    .hero-copy h1 {
        font-size: 44px;
    }

    .hero-copy > p {
        font-size: 14px;
    }

    .hero-actions {
        align-items: stretch;
        flex-direction: column;
    }

    .hero-trust {
        flex-wrap: wrap;
    }

    .hero-illustration {
        width: 112%;
        max-width: none;
        margin-left: -6%;
    }

    .section-heading h2 {
        font-size: 34px;
    }

    .solution-copy {
        padding: 38px 28px;
    }

    .solution-visual {
        min-height: 300px;
    }

    .scene-screen {
        top: 15%;
        right: 5%;
        bottom: 15%;
        left: 5%;
        border-radius: 13px;
    }

    .scene-screen-bar {
        height: 31px;
        padding: 0 9px;
    }

    .scene-media {
        inset: 31px 0 0;
    }

    .remote-device {
        bottom: 5%;
        left: 3%;
        width: 118px;
        height: 64px;
        border-width: 4px;
        border-radius: 13px;
    }

    .capability-grid {
        grid-template-columns: 1fr;
    }

    .capability-grid article {
        min-height: 190px;
    }

    .platform-mesh {
        width: 108%;
        margin-left: -4%;
    }

    .final-cta {
        padding: 40px 26px;
        border-radius: 18px !important;
    }
}

</style>
