/** 与 gr_web_client connect_token 对齐: ?c= URL-safe Base64(JSON{d,p?,m?}) */

function encodeConnectToken(input: {
  deviceId: string
  password?: string
  pwdMd5?: string
}): string {
  const body: Record<string, string> = { d: input.deviceId }
  if (input.password) body.p = input.password
  if (input.pwdMd5) body.m = input.pwdMd5
  const bytes = new TextEncoder().encode(JSON.stringify(body))
  let bin = ''
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]!)
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '')
}

export function buildWebClientUrl(
  ip: string,
  port: string | number,
  input: { deviceId: string; password?: string; pwdMd5?: string },
): string {
  return `http://${ip}:${port}/web_client/?c=${encodeConnectToken(input)}`
}
