// Sync message protos from the single source of truth (src/px_deps/px_message)
// into the web clients' proto dirs. These dirs are gitignored / generated.
//
// Wired into:
//   - scripts_build\build_official.bat (before the web frontend build)
//   - web/px_web_client package.json "predev"/"prebuild"
//   - src/px_web_client package.json "predev"/"prebuild"
import fs from 'node:fs'
import path from 'node:path'

const repo = path.resolve(import.meta.dirname, '..')
const src = path.join(repo, 'src', 'px_deps', 'px_message')

const targets = [
  {
    dir: 'web/px_web_client/proto',
    files: ['px_message.proto', 'px_file_transfer.proto'],
  },
  {
    dir: 'src/px_web_client/proto',
    files: ['px_message.proto', 'px_client_panel_message.proto', 'px_file_transfer.proto'],
  },
]

for (const { dir } of targets) {
  fs.mkdirSync(path.join(repo, dir), { recursive: true })
  for (const retiredProto of ['px_signaling_message.proto', 'relay_message.proto']) {
    const retiredPath = path.join(repo, dir, retiredProto)
    fs.rmSync(retiredPath, { force: true, maxRetries: 3, retryDelay: 50 })
  }
}

for (const { dir, files } of targets) {
  const out = path.join(repo, dir)
  for (const f of files) {
    fs.copyFileSync(path.join(src, f), path.join(out, f))
    console.log(`synced ${dir}/${f}`)
  }
}
