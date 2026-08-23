export class FileTransfer {
  constructor(
    public the_file_id: string,
    public visitor_device: string,
    public target_device: string,
    public begin: number = 0,
    public end: number = 0,
    public total: number,
    public file_detail: string,
    public created_timestamp: number = 0,
    public direction: string = '',
    public success?: boolean,
    public duration?: number,
    public status: string = '',
    public end_reason: string = '',
    public recovered: boolean = false,
  ) {}
}
