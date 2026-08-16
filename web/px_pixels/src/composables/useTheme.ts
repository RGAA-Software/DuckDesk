import { ref, watch } from 'vue'

const STORAGE_KEY = 'px-theme'

function detect(): boolean {
  const saved = localStorage.getItem(STORAGE_KEY)
  if (saved === 'dark') return true
  if (saved === 'light') return false
  return window.matchMedia('(prefers-color-scheme: dark)').matches
}

const isDark = ref(detect())

watch(isDark, (v) => {
  document.documentElement.dataset.theme = v ? 'dark' : 'light'
  try {
    localStorage.setItem(STORAGE_KEY, v ? 'dark' : 'light')
  } catch {
    /* 隐私模式下忽略 */
  }
})

/* 初始化（与 index.html 内联脚本保持一致） */
document.documentElement.dataset.theme = isDark.value ? 'dark' : 'light'

export function useTheme() {
  function toggle() {
    isDark.value = !isDark.value
  }

  return { isDark, toggle }
}
