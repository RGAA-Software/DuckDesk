import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import type { Locale } from '../i18n'

const STORAGE_KEY = 'px-lang'

export function useLocale() {
  const { locale } = useI18n()

  const isZh = computed(() => locale.value === 'zh-CN')

  function setLocale(selectedLocale: Locale) {
    locale.value = selectedLocale
    document.documentElement.lang = selectedLocale
    try {
      localStorage.setItem(STORAGE_KEY, selectedLocale)
    } catch {
      /* 隐私模式下忽略 */
    }
  }

  return { locale, isZh, setLocale }
}
