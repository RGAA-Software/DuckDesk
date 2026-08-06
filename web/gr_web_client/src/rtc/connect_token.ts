/** Web 直连参数:用 URL 安全 Base64 包一层,避免 ?deviceId=&password= 明文暴露。
 *  载荷 JSON: { d: deviceId, p?: password, m?: pwd_md5 }
 *  查询参数: ?c=<token>
 *  仍兼容旧明文 query,便于本地调试。
 */

export interface ConnectTokenPayload {
  deviceId: string
  password: string
  pwdMd5: string
}

function utf8ToBytes(text: string): Uint8Array {
  return new TextEncoder().encode(text)
}

function bytesToUtf8(bytes: Uint8Array): string {
  return new TextDecoder().decode(bytes)
}

function bytesToBase64Url(bytes: Uint8Array): string {
  let bin = ''
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]!)
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '')
}

function base64UrlToBytes(token: string): Uint8Array {
  const b64 = token.replace(/-/g, '+').replace(/_/g, '/')
  const pad = b64.length % 4 === 0 ? '' : '='.repeat(4 - (b64.length % 4))
  const bin = atob(b64 + pad)
  const out = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i)
  return out
}

/** 编码连接参数为 ?c= token */
export function encodeConnectToken(input: {
  deviceId: string
  password?: string
  pwdMd5?: string
}): string {
  const body: Record<string, string> = { d: input.deviceId }
  if (input.password) body.p = input.password
  if (input.pwdMd5) body.m = input.pwdMd5
  return bytesToBase64Url(utf8ToBytes(JSON.stringify(body)))
}

/** 解码 ?c= token;失败返回 null */
export function decodeConnectToken(token: string): ConnectTokenPayload | null {
  if (!token) return null
  try {
    const raw = bytesToUtf8(base64UrlToBytes(token.trim()))
    const obj = JSON.parse(raw) as { d?: string; p?: string; m?: string }
    if (!obj.d) return null
    return {
      deviceId: String(obj.d),
      password: obj.p ? String(obj.p) : '',
      pwdMd5: obj.m ? String(obj.m) : '',
    }
  } catch {
    return null
  }
}

/** 拼 web_client 入口 URL */
export function buildWebClientUrl(
  base: string,
  input: { deviceId: string; password?: string; pwdMd5?: string },
): string {
  const root = base.endsWith('/') ? base : `${base}/`
  return `${root}?c=${encodeConnectToken(input)}`
}
