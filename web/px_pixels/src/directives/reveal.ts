import type { Directive } from 'vue'

/**
 * v-reveal 指令：元素进入视口时添加 .reveal-visible 触发入场动画。
 * 用法：v-reveal 或 v-reveal="120"（延迟毫秒，用于级联动画）
 * 配合 main.css 中的 .reveal / .reveal-visible 样式。
 */

interface RevealHTMLElement extends HTMLElement {
  __revealObserver?: IntersectionObserver
}

const reveal: Directive<RevealHTMLElement, number | undefined> = {
  mounted(el, binding) {
    el.classList.add('reveal')

    const delay = typeof binding.value === 'number' ? binding.value : 0
    if (delay > 0) {
      el.style.transitionDelay = `${delay}ms`
    }

    // 无障碍：用户偏好减少动态时直接呈现
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
      el.classList.add('reveal-visible')
      return
    }

    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) {
            el.classList.add('reveal-visible')
            observer.disconnect()
          }
        }
      },
      { threshold: 0.15 },
    )

    observer.observe(el)
    el.__revealObserver = observer
  },
  unmounted(el) {
    el.__revealObserver?.disconnect()
    delete el.__revealObserver
  },
}

export default reveal
