export class Visit {
  constructor(
    public conn_id: string,
    public stream_id: string,
    public conn_type: string,
    public visitor_device: string,
    public target_device: string,
    public begin: number,
    public end: number,
    public duration: number,
    public total: number,
    public status: string = '',
    public end_reason: string = '',
    public recovered: boolean = false,
    public created_timestamp: number = 0,
  ) {}
}
