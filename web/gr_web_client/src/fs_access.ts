// File System Access API 的最小类型封装(showDirectoryPicker/createWritable 不在 lib.dom 里)
// Chrome/Edge 支持;不支持时调用方走降级路径(暂存区 + <a download>)
export interface FsWritable {
  write(data: Uint8Array | Blob | string): Promise<void>
  close(): Promise<void>
}

export interface FsFileHandle {
  kind: 'file'
  name: string
  getFile(): Promise<File>
  createWritable(): Promise<FsWritable>
}

export interface FsDirHandle {
  kind: 'directory'
  name: string
  getFileHandle(name: string, opts?: { create?: boolean }): Promise<FsFileHandle>
  getDirectoryHandle(name: string, opts?: { create?: boolean }): Promise<FsDirHandle>
  values(): AsyncIterable<FsFileHandle | FsDirHandle>
}

export interface FsEntry {
  name: string
  kind: 'file' | 'directory'
  size: number
  handle: FsFileHandle | FsDirHandle
}

export const fsAccessSupported =
  typeof (window as unknown as { showDirectoryPicker?: unknown }).showDirectoryPicker === 'function'

// 弹系统目录选择器;用户取消返回 null
export async function pickDirectory(): Promise<FsDirHandle | null> {
  const w = window as unknown as {
    showDirectoryPicker?: (opts?: { mode?: string }) => Promise<FsDirHandle>
  }
  if (!w.showDirectoryPicker) return null
  try {
    // readwrite:下载落盘需要在目录里创建文件
    return await w.showDirectoryPicker({ mode: 'readwrite' })
  } catch {
    return null // 用户取消 / 权限被拒
  }
}

// 列出目录内容(文件夹在前,各自按名字排序);文件带大小
export async function listDir(dir: FsDirHandle): Promise<FsEntry[]> {
  const entries: FsEntry[] = []
  for await (const handle of dir.values()) {
    let size = 0
    if (handle.kind === 'file') {
      try {
        size = (await (handle as FsFileHandle).getFile()).size
      } catch {
        /* 读取失败按 0 处理 */
      }
    }
    entries.push({ name: handle.name, kind: handle.kind, size, handle })
  }
  entries.sort((a, b) => {
    if (a.kind !== b.kind) return a.kind === 'directory' ? -1 : 1
    return a.name.localeCompare(b.name)
  })
  return entries
}

// 把数据写入目录下的指定文件(已存在则截断覆盖)
export async function writeFile(dir: FsDirHandle, name: string, data: Uint8Array): Promise<void> {
  const fh = await dir.getFileHandle(name, { create: true })
  const w = await fh.createWritable()
  try {
    await w.write(data)
  } finally {
    await w.close()
  }
}

// 逐级创建/进入子目录(文件夹下载时还原目录结构用),返回最深层句柄
export async function ensureDir(root: FsDirHandle, segments: string[]): Promise<FsDirHandle> {
  let cur = root
  for (const seg of segments) {
    if (!seg) continue
    cur = await cur.getDirectoryHandle(seg, { create: true })
  }
  return cur
}
