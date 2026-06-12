/**
 * 使用现代 Clipboard API 写入剪切板
 * @param text 要复制的文本
 * @returns Promise<boolean> 是否成功
 */
export async function copyText(text: string): Promise<boolean> {
  if (!navigator?.clipboard?.writeText) {
    // 降级方案
    fallbackCopy(text)
    return false
  }

  await navigator.clipboard.writeText(text)
  return true
}

function fallbackCopy(text: string) {
  const textarea = document.createElement('textarea')
  textarea.value = text
  textarea.style.position = 'fixed'
  textarea.style.opacity = '0'
  document.body.appendChild(textarea)
  textarea.select()
  document.execCommand('copy')
  document.body.removeChild(textarea)
}
