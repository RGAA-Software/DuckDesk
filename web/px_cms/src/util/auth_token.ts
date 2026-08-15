import CryptoJS from 'crypto-js'

const APP_SECRET_SALT = 'bfa900206bed4db59156ae5fead1d249'

export function calculateAppSecret(appkey: string): string {
  const shaHex = CryptoJS.SHA256(appkey + APP_SECRET_SALT).toString()
  return CryptoJS.MD5(shaHex).toString()
}

export function generateNonce(): string {
  const bytes = new Uint8Array(16)
  crypto.getRandomValues(bytes)
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
}

export interface ConnectionToken {
  token: string
  ts: number
  nonce: string
}

export function generateConnectionToken(appkey: string): ConnectionToken
export function generateConnectionToken(
  appkey: string,
  ts: number,
  nonce: string,
): ConnectionToken
export function generateConnectionToken(
  appkey: string,
  ts?: number,
  nonce?: string,
): ConnectionToken {
  const finalTs = ts ?? Date.now()
  const finalNonce = nonce ?? generateNonce()
  const appSecret = calculateAppSecret(appkey)
  const message = `${appkey}|${finalTs}|${finalNonce}`
  const token = CryptoJS.HmacSHA256(message, appSecret).toString()
  return { token, ts: finalTs, nonce: finalNonce }
}
