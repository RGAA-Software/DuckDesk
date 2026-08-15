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
  ) {}
}
