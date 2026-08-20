export function validateAdminUsername(value: string): string | undefined {
  const length = [...value].length
  if (length < 3 || length > 64) return '用户名需要 3–64 个字符'
  if (value.trim() !== value) return '用户名首尾不能包含空格'
  if (value.includes('/') || value.includes('\\')) return '用户名不能包含 / 或 \\'
  if (/[\u0000-\u001f\u007f-\u009f]/u.test(value)) return '用户名不能包含控制字符'
  return undefined
}

export function validateInitialPassword(value: string): string | undefined {
  if (!value) return undefined
  const length = [...value].length
  if (length < 8 || length > 128) return '手动设置的密码需要 8–128 个字符；也可以留空自动生成'
  if (!value.trim()) return '密码不能全部是空格'
  return undefined
}
