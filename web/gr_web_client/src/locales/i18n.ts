import { createI18n } from 'vue-i18n'
import messages, { type AppLocale } from './index'

const LS_LANG = 'gr_web_client.language'

function detectLocale(): AppLocale {
  try {
    const fromUrl = new URLSearchParams(window.location.search).get('lang')
    if (fromUrl === 'en' || fromUrl === 'zh') return fromUrl
  } catch {
    /* ignore */
  }
  try {
    const saved = localStorage.getItem(LS_LANG)
    if (saved === 'en' || saved === 'zh') return saved
  } catch {
    /* ignore */
  }
  const nav = (navigator.language || 'zh').toLowerCase()
  return nav.startsWith('zh') ? 'zh' : 'en'
}

export const i18n = createI18n({
  legacy: false,
  globalInjection: true,
  locale: detectLocale(),
  fallbackLocale: 'zh',
  messages,
  silentTranslationWarn: true,
  missingWarn: false,
  fallbackWarn: false,
})

/** Last device id used in document.title (kept across locale switches). */
let titleDeviceId = ''

export function setAppLocale(locale: AppLocale) {
  i18n.global.locale.value = locale
  try {
    localStorage.setItem(LS_LANG, locale)
  } catch {
    /* ignore */
  }
  applyDocumentTitle()
  document.documentElement.lang = locale === 'zh' ? 'zh-CN' : 'en'
}

export function applyDocumentTitle(deviceId?: string) {
  if (deviceId !== undefined) titleDeviceId = deviceId.trim()
  document.title = titleDeviceId
    ? String(i18n.global.t('app.titleWithId', { id: titleDeviceId }))
    : String(i18n.global.t('app.title'))
  document.documentElement.lang = i18n.global.locale.value === 'zh' ? 'zh-CN' : 'en'
}

export { LS_LANG }
