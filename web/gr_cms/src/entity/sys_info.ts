/* ================= CPU ================= */

export class SysSingleCpuInfo {
  constructor(
    public name: string = '',
    public usage: number = 0,
  ) {}
}

export class SysCpuInfo {
  constructor(
    public usage: number = 0,
    public vendor: string = '',
    public brand: string = '',
    public base_frequency: number = 0,
    public current_frequency: number = 0,
    public max_frequency: number = 0,
    public cpus: SysSingleCpuInfo[] = [],
  ) {}
}

/* ================= Memory ================= */

export class SysMemInfo {
  constructor(
    public total: number = 0,
    public total_gb: number = 0,
    public used: number = 0,
    public used_gb: number = 0,
    public available: number = 0,
    public available_gb: number = 0,
  ) {}
}

/* ================= Disk ================= */

export class SysDiskInfo {
  constructor(
    public disk_type: string = '',
    public mount_on: string = '',
    public filesystem: string = '',
    public available: number = 0,
    public available_gb: number = 0,
    public total: number = 0,
    public total_gb: number = 0,
  ) {}
}

/* ================= Network ================= */

export class SysIpNetwork {
  constructor(
    public addr: string = '',
    public prefix: number = 0,
  ) {}
}

export class SysNetworkInfo {
  constructor(
    public name: string = '',
    public mac: string = '',
    public ip_networks: SysIpNetwork[] = [],
    public received_data: number = 0,
    public sent_data: number = 0,
    public max_transmit_speed: number = 0,
    public max_receive_speed: number = 0,
  ) {}
}

/* ================= User ================= */

export class SysUserInfo {
  // 目前 Rust 里是空 struct
  constructor() {}
}

/* ================= OS ================= */

export class SysOsInfo {
  constructor(
    public sys_name: string = '',
    public sys_kernel_version: string = '',
    public sys_os_version: string = '',
    public sys_os_long_version: string = '',
    public sys_host_name: string = '',
    public sys_kernel: string = '',
  ) {}
}

/* ================= Component ================= */

export class SysComponentInfo {
  constructor(
    public temperature: number = 0,
    public max: number = 0,
    public critical: number = 0,
    public label: string = '',
  ) {}
}

/* ================= GPU ================= */

export class SysGpuInfo {
  constructor(
    public id: string = '',
    public brand: string = '',
    public fan_speed: number = 0,
    public power_limit: number = 0,
    public encoder_utilization: number = 0,
    public gpu_utilization: number = 0,
    public mem_utilization: number = 0,
    public temperature: number = 0,
    public mem_free: number = 0,
    public mem_free_gb: number = 0,
    public mem_used: number = 0,
    public mem_used_gb: number = 0,
    public mem_total: number = 0,
    public mem_total_gb: number = 0,
  ) {}
}

/* ================= Root ================= */

export class SysInfo {
  constructor(
    public timestamp: number = 0,
    public timestamp_readable: string = '',
    public cpu: SysCpuInfo = new SysCpuInfo(),
    public mem: SysMemInfo = new SysMemInfo(),
    public disks: SysDiskInfo[] = [],
    public networks: SysNetworkInfo[] = [],
    public os: SysOsInfo = new SysOsInfo(),
    public components: SysComponentInfo[] = [],
    public uptime: string = '',
    public gpus: SysGpuInfo[] = [],
  ) {}
}
