<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { contactInfo, navItems } from '../data/content'
import PixelLogo from './PixelLogo.vue'
import PixelsWord from './PixelsWord.vue'

const { t } = useI18n()
</script>

<template>
  <footer class="footer">
    <div class="container">
      <div class="footer-top">
        <div>
          <a class="footer-brand" href="#top">
            <span class="footer-mark">
              <PixelLogo :size="34" />
            </span>
            <PixelsWord :height="18" class="footer-word" />
          </a>
          <p class="footer-desc">{{ t('footer.desc') }}</p>
        </div>

        <div>
          <h4>{{ t('footer.quickNav') }}</h4>
          <div class="footer-links">
            <a v-for="n in navItems" :key="n.key" :href="n.href">{{ t(`nav.${n.key}`) }}</a>
            <a href="#contact">{{ t('nav.contact') }}</a>
          </div>
        </div>

        <div>
          <h4>{{ t('footer.contact') }}</h4>
          <ul class="footer-contact">
            <li>
              <b class="px-mono">PHONE</b>
              <span>{{ contactInfo.phone }}</span>
            </li>
            <li>
              <b class="px-mono">EMAIL</b>
              <span>{{ contactInfo.email }}</span>
            </li>
            <li>
              <b class="px-mono">ADDRESS</b>
              <span>{{ contactInfo.address }}</span>
            </li>
          </ul>
        </div>
      </div>

      <div class="footer-bottom">
        <span>{{ t('footer.copyright') }}</span>
        <span class="px-mono">{{ t('footer.icp') }}</span>
      </div>
    </div>
  </footer>
</template>

<style scoped>
.footer {
  padding-top: 64px;
  color: rgba(255, 255, 255, 0.75);
  background: var(--px-green-deep);
}

.footer-top {
  display: grid;
  grid-template-columns: 1.4fr 1fr 1fr;
  gap: 40px;
  padding-bottom: 44px;
}

.footer-brand {
  display: inline-flex;
  align-items: center;
  gap: 12px;
}

.footer-mark {
  color: var(--px-green);
  line-height: 0;
}

.footer-word {
  color: #fff;
}

.footer-desc {
  margin-top: 16px;
  max-width: 320px;
  font-size: 14px;
  line-height: 1.9;
  color: rgba(255, 255, 255, 0.55);
}

.footer h4 {
  margin: 2px 0 16px;
  font-size: 15px;
  color: #fff;
}

.footer-links {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.footer-links a {
  font-size: 14px;
  color: rgba(255, 255, 255, 0.65);
  transition: color 0.2s;
}

.footer-links a:hover {
  color: var(--px-green);
}

.footer-contact {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.footer-contact b {
  display: block;
  font-size: 11px;
  font-weight: 400;
  letter-spacing: 2px;
  color: rgba(255, 255, 255, 0.4);
}

.footer-contact span {
  display: block;
  margin-top: 3px;
  font-size: 14px;
  color: rgba(255, 255, 255, 0.65);
}

.footer-bottom {
  display: flex;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
  padding: 18px 0;
  border-top: 1px solid rgba(255, 255, 255, 0.12);
  font-size: 13px;
  color: rgba(255, 255, 255, 0.4);
}

@media (max-width: 960px) {
  .footer-top {
    grid-template-columns: 1fr;
    gap: 32px;
  }
}
</style>
