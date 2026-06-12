export class SpvrUser {
  constructor(
    public uid: string,
    public username: string = '',
    public password: string = '',
    public assigned: boolean = false,
    public created_timestamp: number = 0,
    public update_timestamp: number = 0,
    public deleted: boolean = false,
    public avatar_path: string = '',
    public administrator: boolean = false,
    public total: number = 0,
  ) {}
}
