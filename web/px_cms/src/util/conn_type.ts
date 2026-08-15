export type ConnTypeTagType = 'success' | 'primary' | 'warning' | 'info' | 'danger'

const CONN_TYPE_LABELS: Record<string, string> = {
  Direct: '直连',
  Relay: '中继',
  RTC: 'WebRTC',
  P2P: 'P2P',
  UDP: 'UDP',
}

const CONN_TYPE_TAG_TYPES: Record<string, ConnTypeTagType> = {
  Direct: 'success',
  Relay: 'primary',
  RTC: 'info',
  P2P: 'warning',
  UDP: 'warning',
}

export function formatConnTypeLabel(connType: string | undefined | null): string {
  if (!connType) {
    return '未知'
  }
  return CONN_TYPE_LABELS[connType] ?? connType
}

export function connTypeTagType(connType: string | undefined | null): ConnTypeTagType {
  if (!connType) {
    return 'info'
  }
  return CONN_TYPE_TAG_TYPES[connType] ?? 'primary'
}
