import { describe, expect, it } from 'vitest'

import {
  fileTransferReasonLabel,
  fileTransferStatusColor,
  fileTransferStatusLabel,
  normalizedFileTransferStatus,
} from './file_transfer_terminal'

describe('file transfer terminal presentation', () => {
  it('keeps legacy records compatible', () => {
    expect(normalizedFileTransferStatus({ end: 0, success: false })).toBe('running')
    expect(normalizedFileTransferStatus({ end: 10, success: true })).toBe('succeeded')
    expect(normalizedFileTransferStatus({ end: 10, success: false })).toBe('failed')
  })

  it('renders all structured terminal states', () => {
    expect(fileTransferStatusLabel({ status: 'cancelled' })).toBe('已取消')
    expect(fileTransferStatusLabel({ status: 'skipped' })).toBe('已跳过')
    expect(fileTransferStatusLabel({ status: 'aborted' })).toBe('异常结束')
    expect(fileTransferStatusColor({ status: 'failed' })).toBe('error')
    expect(fileTransferStatusColor({ status: 'aborted' })).toBe('warning')
  })

  it('renders stable reasons and preserves unknown future reasons', () => {
    expect(fileTransferReasonLabel('integrity_mismatch')).toContain('完整性')
    expect(fileTransferReasonLabel('transport_disconnected')).toContain('断开')
    expect(fileTransferReasonLabel('future_reason')).toBe('future_reason')
    expect(fileTransferReasonLabel('')).toBe('-')
  })
})
