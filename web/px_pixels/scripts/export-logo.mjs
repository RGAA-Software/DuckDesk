/* 导出 PIXELS Logo PNG（256×256 / 512×512，透明背景，等比居中）
   用法：node scripts/export-logo.mjs（需要 sharp 已安装） */
import sharp from 'sharp'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const svg = path.join(root, 'public', 'logo.svg')

for (const size of [256, 512]) {
  const out = path.join(root, 'public', `logo-${size}.png`)
  await sharp(svg)
    .resize(size, size, { fit: 'contain', background: { r: 0, g: 0, b: 0, alpha: 0 } })
    .png()
    .toFile(out)
  const meta = await sharp(out).metadata()
  console.log(`${path.basename(out)}: ${meta.width}x${meta.height} (${meta.format})`)
}
