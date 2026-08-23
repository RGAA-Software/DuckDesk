import { computed, ref } from 'vue'
import { theme } from 'ant-design-vue'

const THEME_KEY = 'gammaray-console-theme' // 'light' | 'dark'

// 模块级状态：全局共享同一个 isDark
const isDark = ref(false)

function applyTheme(dark: boolean) {
  // html.dark 用于页面背景等少量自定义 CSS；antd 组件自身靠 darkAlgorithm 切换
  document.documentElement.classList.toggle('dark', dark)
  isDark.value = dark
}

/** 供 main.ts 启动时调用，避免主题闪烁。 */
export function initTheme() {
  const saved = localStorage.getItem(THEME_KEY)
  applyTheme(saved === 'dark')
}

export function useTheme() {
  const toggleTheme = () => {
    const next = !isDark.value
    applyTheme(next)
    localStorage.setItem(THEME_KEY, next ? 'dark' : 'light')
  }

  // 「知识写作」淡绿色主题：主色 #52c41a，亮暗切换只换算法
  const themeConfig = computed(() => ({
    algorithm: isDark.value ? theme.darkAlgorithm : theme.defaultAlgorithm,
    token: {
      colorPrimary: '#52c41a',
      borderRadius: 6,
    },
  }))

  return { isDark, toggleTheme, themeConfig }
}
