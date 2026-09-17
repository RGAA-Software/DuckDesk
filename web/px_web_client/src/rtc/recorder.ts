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
    const mime =
      MIME_CANDIDATES.find((mimeType) => MediaRecorder.isTypeSupported(mimeType)) ?? ''
    this.chunks = []
    this.mimeType = mime
    this.recorder = new MediaRecorder(stream, mime ? { mimeType: mime } : undefined)
    this.recorder.ondataavailable = (blobEvent: BlobEvent) => {
      if (blobEvent.data.size > 0) this.chunks.push(blobEvent.data)
    }
    this.startedAt = Date.now()
    // 1s 分段出数据,停止时能拿到完整时长
    this.recorder.start(1000)
  }

  stop(): Promise<RecordResult> {
    return new Promise((resolve, reject) => {
      const recorder = this.recorder
      if (!recorder || recorder.state === 'inactive') {
        reject(new Error('未在录制'))
        return
      }
      recorder.onstop = () => {
        const blob = new Blob(this.chunks, { type: this.mimeType || 'video/webm' })
        this.recorder = null
        resolve({ blob, mimeType: blob.type, seconds: (Date.now() - this.startedAt) / 1000 })
      }
      recorder.onerror = (recordingEvent: Event) =>
        reject(new Error(`录制出错: ${String(recordingEvent)}`))
      recorder.stop()
    })
  }
}

// 文件名带时间戳:gr-record-20260802-012345.webm
export function recordFileName(recordingDate = new Date()): string {
  const padTimePart = (timePart: number) => String(timePart).padStart(2, '0')
  const timestamp = `${recordingDate.getFullYear()}${padTimePart(recordingDate.getMonth() + 1)}${padTimePart(recordingDate.getDate())}-${padTimePart(recordingDate.getHours())}${padTimePart(recordingDate.getMinutes())}${padTimePart(recordingDate.getSeconds())}`
  return `gr-record-${timestamp}.webm`
}
