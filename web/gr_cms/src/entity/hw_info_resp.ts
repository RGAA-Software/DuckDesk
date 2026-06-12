import type { SysInfo } from '@/entity/sys_info.ts'
import { WsBaseMsg } from '@/entity/ws_base_msg.ts'

export class HwInfoResp extends WsBaseMsg {
  constructor(
    public device_id: string,
    public sys_info: SysInfo,
  ) {
    super('stream_hardware_piece_resp')
  }
}
