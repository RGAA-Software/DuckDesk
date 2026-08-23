// request device render records (docs/console_render_records_view_design.md 5.2/5.3/6.3/6.4)
import axiosHttp from '@/http.ts'
import { AxiosError } from 'axios'

export interface RecordAccessInfo {
  device_id: string
  panel_lan_ips: string[]
  panel_port: number
  online: boolean
}

export interface RecordTicket {
  tk: string
  // unix seconds
  exp: number
}

// one file entry of GET /api/v1/record/list
export interface RecordFileItem {
  // c_records id ("{device_id}:{filename}"), "" when the file is panel-only
  id: string
  name: string
  size: number
  // device file mtime, unix seconds
  mtime: number
  monitor: string
  codec: string
  // "none" (only on device) | "fetching" | "ready" | "error"
  state: string
  keep: boolean
  // bytes received so far (state == fetching)
  progress: number
  // expected total bytes (state == fetching)
  total: number
  // playable url when state == ready: /uploads/records/{device_id}/{name}
  url: string
}

export interface RecordListResp {
  device_id: string
  files: RecordFileItem[]
}

export interface RecordFetchResp {
  // "fetching" | "ready"
  state: string
  url: string
}

export interface RecordDownloadResp {
  // "downloading" | "ready"
  state: string
  id: string
  url: string
}

// business codes from console_api_error.rs
export const RECORD_ERR_DEVICE_OFFLINE = 626
export const RECORD_ERR_TIMEOUT = 627

// api error carrying the console business code (RespMessage.code)
export class RecordApiError extends Error {
  code: number

  constructor(code: number, message: string) {
    super(message)
    this.code = code
  }
}

// unwrap axios error -> RecordApiError with the business code when present
function toRecordApiError(e: unknown): RecordApiError {
  if (e instanceof AxiosError && e.response?.data) {
    const data = e.response.data
    if (typeof data.code === 'number') {
      return new RecordApiError(data.code, data.message || `request failed: ${e.response.status}`)
    }
    return new RecordApiError(e.response.status, `request failed: ${e.response.status}`)
  }
  if (e instanceof RecordApiError) {
    return e
  }
  return new RecordApiError(-1, e instanceof Error ? e.message : String(e))
}

// unwrap the RespMessage envelope; throws RecordApiError on failure
function unwrap<T>(resp: { status: number; data: { code: number; message: string; data: T } }): T {
  const data = resp.data
  if (resp.status !== 200 || data.code !== 200) {
    throw new RecordApiError(data.code, data.message || 'request failed')
  }
  return data.data
}

// GET /api/v1/record/access?device_id= — topology entry (design 5.2)
export async function getRecordAccess(deviceId: string): Promise<RecordAccessInfo> {
  try {
    const resp = await axiosHttp.get('/api/v1/record/access', {
      params: { device_id: deviceId },
    })
    return unwrap<RecordAccessInfo>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}

// GET /api/v1/record/ticket?device_id=&file= — short-lived ticket (design 5.3);
// file = "*" covers list / info requests
export async function getRecordTicket(deviceId: string, file: string): Promise<RecordTicket> {
  try {
    const resp = await axiosHttp.get('/api/v1/record/ticket', {
      params: { device_id: deviceId, file: file },
    })
    return unwrap<RecordTicket>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}

// GET /api/v1/record/list?device_id= — console-side merged list (design 6.3)
export async function getRecordList(deviceId: string): Promise<RecordListResp> {
  try {
    const resp = await axiosHttp.get('/api/v1/record/list', {
      params: { device_id: deviceId },
    })
    return unwrap<RecordListResp>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}

// GET /api/v1/record/fetch?device_id=&file= — trigger the tunnel fetch (design 6.3)
export async function fetchRecord(deviceId: string, file: string): Promise<RecordFetchResp> {
  try {
    const resp = await axiosHttp.get('/api/v1/record/fetch', {
      params: { device_id: deviceId, file: file },
    })
    return unwrap<RecordFetchResp>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}

// POST /api/v1/record/download — save a copy on the console host, keep=true (design 6.4)
export async function downloadRecordToConsole(
  deviceId: string,
  filename: string,
): Promise<RecordDownloadResp> {
  try {
    const resp = await axiosHttp.post(
      '/api/v1/record/download',
      { device_id: deviceId, filename: filename },
    )
    return unwrap<RecordDownloadResp>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}

// DELETE /api/v1/record/{id} — id = "{device_id}:{filename}", url-encoded
export async function deleteRecord(id: string): Promise<void> {
  try {
    const resp = await axiosHttp.delete(
      '/api/v1/record/' + encodeURIComponent(id),
    )
    unwrap<string>(resp)
  } catch (e) {
    throw toRecordApiError(e)
  }
}
