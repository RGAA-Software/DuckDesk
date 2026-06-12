export class ClientConn {
  constructor(
    public conn_id: string,
    public device_id: string,
    public remote_device_id: string,
    public remote_device_ip: string,
    public hello_timestamp: number,
    public readable_hello_ts: string,
    public last_update_timestamp: number,
    public readable_update_ts: string,
    public total: number,
  ) {}
}
