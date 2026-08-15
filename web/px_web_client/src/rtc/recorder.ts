// 本地录制:MediaRecorder 把 video.srcObject 的 MediaStream 录成 webm
// 优先 vp9,回落 vp8;流里有音频轨(render 开了声音采集)会一并录进去

export interface RecordResult {
  blob: Blob
  mimeType: string
  seconds: number
}

const MIME_CANDIDATES = [
  'video/webm;codecs=vp9,opus',
  'video/webm;codecs=vp8,opus',
  'video/webm',
]

export class SessionRecorder {
  private recorder: MediaRecorder | null = null
  private chunks: Blob[] = []
  private startedAt = 0
  // 实际使用的 mimeType(可能为空串 = 浏览器默认)
  mimeType = ''

  static supported(): boolean {
    return typeof MediaRecorder !== 'undefined'
  }

  get recording(): boolean {
    return this.recorder?.state === 'recording'
  }

  start(stream: MediaStream): void {
    if (this.recording) throw new Error('已在录制中')
    const mime = MIME_CANDIDATES.find((m) => MediaRecorder.isTypeSupported(m)) ?? ''
    this.chunks = []
    this.mimeType = mime
    this.recorder = new MediaRecorder(stream, mime ? { mimeType: mime } : undefined)
    this.recorder.ondataavailable = (ev: BlobEvent) => {
      if (ev.data.size > 0) this.chunks.push(ev.data)
    }
    this.startedAt = Date.now()
    // 1s 分段出数据,停止时能拿到完整时长
    this.recorder.start(1000)
  }

  stop(): Promise<RecordResult> {
    return new Promise((resolve, reject) => {
      const r = this.recorder
      if (!r || r.state === 'inactive') {
        reject(new Error('未在录制'))
        return
      }
      r.onstop = () => {
        const blob = new Blob(this.chunks, { type: this.mimeType || 'video/webm' })
        this.recorder = null
        resolve({ blob, mimeType: blob.type, seconds: (Date.now() - this.startedAt) / 1000 })
      }
      r.onerror = (ev: Event) => reject(new Error(`录制出错: ${String(ev)}`))
      r.stop()
    })
  }
}

// 文件名带时间戳:gr-record-20260802-012345.webm
export function recordFileName(d = new Date()): string {
  const p = (n: number) => String(n).padStart(2, '0')
  const ts = `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`
  return `gr-record-${ts}.webm`
}
