export class Authorization {
  constructor(
    public auth_id: string,
    public auth_name: string,
    public machine_code: string,
    public description: string,
    public max_streams: number,
    public appkey: string,
    public app_secret: string,
    public username: string,
    public password: string,
    public created_timestamp_ms: number,
    public end_timestamp_ms: number,
    public last_modify_timestamp: number,
    public days: number,
    public verify_server: string,
    public deploy_str: string,
    public role: number,
    public used_time_ms: number,
  ) {}
}
