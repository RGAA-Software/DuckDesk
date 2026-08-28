export interface FileTransferTerminalRecord {
  status?: string
  end?: number
  success?: boolean
}

export const normalizedFileTransferStatus = (record: FileTransferTerminalRecord): string => {
  if (record.status) return record.status
  if (!record.end) return 'running'
  return record.success === false ? 'failed' : 'succeeded'
}

export const fileTransferStatusLabel = (record: FileTransferTerminalRecord): string => {
  const status = normalizedFileTransferStatus(record)
  return (
    {
      running: '进行中',
      succeeded: '已完成',
      failed: '失败',
      cancelled: '已取消',
      skipped: '已跳过',
      aborted: '异常结束',
    } as Record<string, string>
  )[status] || status
}

export const fileTransferStatusColor = (record: FileTransferTerminalRecord): string => {
  const status = normalizedFileTransferStatus(record)
  return (
    {
      running: 'processing',
      succeeded: 'success',
      failed: 'error',
      cancelled: 'default',
      skipped: 'default',
      aborted: 'warning',
    } as Record<string, string>
  )[status] || 'default'
}

const reasonLabels: Record<string, string> = {
  completed: '传输完成',
  user_cancelled: '用户取消',
  user_skipped: '用户跳过',
  session_interrupted: '会话中断，可继续传输',
  integrity_mismatch: '文件完整性校验失败，可继续传输',
  integrity_hash_missing: '对端未提供完整性摘要，可继续传输',
  block_sequence_mismatch: '文件块序号异常，可继续传输',
  permission_denied: '没有文件传输权限',
  file_count_limit: '文件数量超过限制',
  source_not_found: '源文件或目录不存在',
  destination_busy: '目标文件被占用，可继续传输',
  io_error: '文件读写失败，可继续传输',
  transport_timeout: '传输连接超时，可继续传输',
  transport_disconnected: '传输连接断开，可继续传输',
  route_unavailable: '传输路由不可用，可继续传输',
  transport_error: '传输通道错误，可继续传输',
  transfer_failed: '文件传输失败，可继续传输',
  client_disconnected: '客户端断开',
  renderer_disconnected: '被控端断开',
  panel_restart_recovery: 'Panel 重启后恢复补录',
}

export const fileTransferReasonLabel = (reason?: string): string => {
  if (!reason) return '-'
  return reasonLabels[reason] || reason
}
