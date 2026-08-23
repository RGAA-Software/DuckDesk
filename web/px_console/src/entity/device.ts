import { SysInfo } from '@/entity/sys_info.ts'

export class Device {
  constructor(
    public created_timestamp: number = 0,
    public desktop_link: string = '',
    public desktop_link_raw: string = '',
    public device_id: string = '',
    public device_name: string = '',
    public gen_random_pwd: string = '',
    public last_update_timestamp: number = 0,
    public logged_in_user_id: string = '',
    public random_pwd_md5: string = '',
    public safety_pwd_md5: string = '',
    public seed: string = '',
    public used_time: string = '',
    public online: boolean = false,
    public device_ip_addr: string = '',
    public active: boolean = false,
    public sys_info: SysInfo = new SysInfo(),
  ) {}
}
