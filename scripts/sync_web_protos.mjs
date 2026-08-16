// Sync message protos from the single source of truth (src/px_deps/px_message_new)
// into the web clients' proto dirs. These dirs are gitignored / generated.
//
// Wired into:
//   - build_official.bat (before the web frontend build)
//   - web/px_web_client package.json "predev"/"prebuild"
//   - src/px_web_client package.json "predev"/"prebuild"
import fs from 'node:fs'
import path from 'node:path'

const repo = path.resolve(import.meta.dirname, '..')
const src = path.join(repo, 'src', 'px_deps', 'px_message_new')

const targets = [
  {
    dir: 'web/px_web_client/proto',
    files: ['px_message.proto', 'px_signaling_message.proto', 'px_file_transfer.proto'],
  },
  {
    dir: 'src/px_web_client/proto',
    files: ['px_message.proto', 'px_signaling_message.proto', 'px_client_panel_message.proto', 'px_file_transfer.proto'],
  },
]

for (const { dir, files } of targets) {
  const out = path.join(repo, dir)
  fs.mkdirSync(out, { recursive: true })
  for (const f of files) {
    fs.copyFileSync(path.join(src, f), path.join(out, f))
    console.log(`synced ${dir}/${f}`)
  }
}
