/** 与 px_web_client connect_token 对齐: ?c= URL-safe Base64(JSON{d,p?,m?})
 *  game-hook 调度另支持明文 ?deviceId=&instanceId=（局域网调试）
 */

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

/** game-hook 实例入口：device_id + instance_id + listen_port */
export function buildGameHookClientUrl(
  ip: string,
  port: string | number,
  input: { deviceId: string; instanceId: string },
): string {
  const q = new URLSearchParams({
    deviceId: input.deviceId,
    instanceId: input.instanceId,
  })
  return `http://${ip}:${port}/web_client/?${q.toString()}`
}
