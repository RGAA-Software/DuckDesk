<script setup lang="ts">
import { useWsStore } from '@/stores/ws.ts'
import { computed, onMounted, reactive, ref, watch } from 'vue'
import {
  IpDevices,
  IpConnectionArrow,
  IpPeoplesTwo,
  IpErrorComputer,
  IpTimer,
} from 'vue-icons-plus/ip'
import LineChart from '@/components/LineChart.vue'
import { SysGpuInfo, SysInfo } from '@/entity/sys_info.ts'
import { HwInfoArrayResp } from '@/entity/hw_info_array_resp.ts'
import type { HwInfoResp } from '@/entity/hw_info_resp.ts'
import { queryDevices } from '@/model/device_api.ts'
import { Device } from '@/entity/device.ts'
import DeviceSelectDialog from '@/components/DeviceSelectDialog.vue'
import axiosHttp from '@/http.ts'
import { formatDuration } from '@/util/time.ts'
import { ElNotification } from 'element-plus'

/// websocket
const wsStore = useWsStore()

/// current online device
const onlineDevice = ref<Device>()

/// total devices
const totalDevices = ref<number>(0)

/// total online connections
const totalOnlineConnections = ref<number>(0)

/// total used time
const totalUsedTime = ref<number>(0)

/// total users
const totalUsers = ref<number>(0)

/// total events
const totalEvents = ref<number>(0)

/// find an online device to display information
const findDeviceToShow = async () => {
  const deviceList = await queryDevices('', '', '', 'online', 1, 5)
  if (!deviceList || deviceList.length === 0) {
    return
  }
  onlineDevice.value = deviceList[0]
  if (onlineDevice.value) {
    wsStore.send({
      msg_type: 'stream_hardware_info',
      device_id: onlineDevice.value.device_id,
      // device_id: '921487172',
    })
  }
}

/// query total devices count
const countDevices = async () => {
  const resp = await axiosHttp.get('/api/v1/device/control/count/devices', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }

  totalDevices.value = data.data
  console.log('total devices: ', data.data)
}

/// query total connection count
const countOnlineConnections = async () => {
  const resp = await axiosHttp.get('/api/v1/client/control/count/alive/conns', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }

  totalOnlineConnections.value = data.data
  console.log('total online connections: ', data.data)
}

/// query total users
const countTotalUsers = async () => {
  const resp = await axiosHttp.get('/api/v1/user/control/count/users', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }

  totalUsers.value = data.data
  console.log('total online connections: ', data.data)
}

/// query total used time
const queryTotalUsedTIme = async () => {
  ///api/v1/device/control/query/total/used/time
  const resp = await axiosHttp.get('/api/v1/device/control/query/total/used/time', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }

  totalUsedTime.value = data.data
  console.log('total online connections: ', totalUsedTime.value)
}

/// query total events
const queryTotalEvents = async () => {
  ///api/v1/device/control/query/total/used/time
  const resp = await axiosHttp.get('/api/v1/event/control/count/events', {
    params: {
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('queryDevices failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    return null
  }

  totalEvents.value = data.data
  console.log('total events: ', totalEvents.value)
}

onMounted(async () => {
  console.log('mounted*****')

  if (wsStore.connected) {
    await findDeviceToShow()
  } else {
    setTimeout(async () => {
      if (!onlineDevice.value) {
        await findDeviceToShow()
      }
    }, 1000)
  }

  // count total devices
  await countDevices()

  // count total online connections
  await countOnlineConnections()

  // count total users
  await countTotalUsers()

  // total used time
  await queryTotalUsedTIme()

  await queryTotalEvents()
})

/// monitor websocket callback
///
watch(
  () => wsStore.connected,
  async (newVal, oldVal) => {
    console.log('connected 变化:', oldVal, '->', newVal)
    await findDeviceToShow()
  },
)

watch(
  () => wsStore.message,
  (msg) => {
    //console.log('watched msg in resource view: ', msg)
    if (msg === null) {
      return
    }

    // 1. change to this device
    // 2. the first callback will be with array
    if (msg.msg_type === 'stream_hardware_info_resp') {
      const hw_info_array = msg as HwInfoArrayResp
      sys_info_array.value = hw_info_array.sys_info_array
      //console.log('sys_info_array: ', sys_info_array.value)
      if (hw_info_array.sys_info_array.length > 1) {
        sysInfo.value = hw_info_array.sys_info_array[0]
      }
      updateStatistics()
    } else if (msg.msg_type === 'stream_hardware_piece_resp') {
      // 3. stream callback each piece
      const hw_info = msg as HwInfoResp
      if (sys_info_array.value?.length && sys_info_array.value?.length >= 180) {
        sys_info_array.value.shift()
      }
      sys_info_array.value?.push(hw_info.sys_info)
      updateStatistics()
    }
  },
)

/// system info
///

const sys_info_array = ref<SysInfo[]>()

/// Cpu Usage
///
const cpuUsageXAxis = ref<string[]>([])
const cpuUsageYAxis = ref<number[]>([])

/// Cpu Frequency
///
const cpuFreqXAxis = ref<string[]>([])
const cpuFreqYAxis = ref<number[]>([])

/// Memory
///
const memTitle = ref<string>('')
const memUsageXAxis = ref<string[]>([])
const memUsageYAxis = ref<number[]>([])

/// Network sending
///
const networkSendTitle = ref<string>('')
const networkSendXAxis = ref<string[]>([])
const networkSendYAxis = ref<number[]>([])

/// network receiving
///
const networkRecvTitle = ref<string>('')
const networkRecvXAxis = ref<string[]>([])
const networkRecvYAxis = ref<number[]>([])

/// gpu info
///
const gpuInfoMap = reactive(new Map<string, SysGpuInfo[]>())

/// sys info
///
const sysInfo = ref<SysInfo>()

const gpuRenderList = computed(() => {
  return Array.from(gpuInfoMap.entries()).map(([key, list]) => {
    return {
      key, // Map key
      brand: list[0]?.brand ?? 'Unknown',
      gpuList: list.map((item) => ({
        model: item.gpu_utilization,
        memory: item.mem_used_gb,
        temperature: item.temperature,
      })),

      tempTitle: 'GPU温度(' + list[0]?.temperature + '℃)',
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      tempXAxis: list.map((item) => ''),
      tempYAxis: list.map((item) => item.temperature),

      usageTitle: 'GPU使用率(' + list[0]?.gpu_utilization + '%)',
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      usageXAxis: list.map((item) => ''),
      usageYAxis: list.map((item) => item.gpu_utilization),

      encoderTitle: 'GPU编码其使用率(' + list[0]?.encoder_utilization + '%)',
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      encoderXAxis: list.map((item) => ''),
      encoderYAxis: list.map((item) => item.encoder_utilization),

      memUsageTitle:
        'GPU显存使用率(' +
        list[0]?.mem_used_gb.toFixed(2) +
        'GB/' +
        list[0]?.mem_total_gb.toFixed(2) +
        'GB)',
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      memUsageXAxis: list.map((item) => ''),
      memUsageYAxis: list.map((item) => item.mem_used_gb),
    }
  })
})

const updateStatistics = () => {
  if (!sys_info_array.value) {
    return
  }
  // cpu usage
  cpuUsageXAxis.value = []
  cpuUsageYAxis.value = []
  const tmpCpuUsageXAxis = []
  const tmpCpuUsageYAxis = []

  // cpu frequency
  cpuFreqXAxis.value = []
  cpuFreqYAxis.value = []
  const tmpCpuFreqXAxis = []
  const tmpCpuFreqYAxis = []

  // mem usage
  memUsageXAxis.value = []
  memUsageYAxis.value = []
  const tmpMemUsageXAxis = []
  const tmpMemUsageYAxis = []

  // network sending speed
  let last_send_data = -1
  const tmpNetworkSendXAxis = []
  const tmpNetworkSendYAxis = []

  // network receiving speed
  let last_recv_data = -1
  const tmpNetworkRecvXAxis = []
  const tmpNetworkRecvYAxis = []

  for (let i = 0; i < sys_info_array.value.length; i++) {
    const item = sys_info_array.value[i]
    if (!item || !item.cpu) {
      continue
    }
    tmpCpuUsageXAxis.push('')
    tmpCpuUsageYAxis.push(item.cpu.usage)

    tmpCpuFreqXAxis.push('')
    tmpCpuFreqYAxis.push(item.cpu.current_frequency)

    tmpMemUsageXAxis.push('')
    tmpMemUsageYAxis.push(item.mem.used / 1024 / 1024 / 1024)

    if (last_send_data === -1) {
      tmpNetworkSendXAxis.push('')
      tmpNetworkSendYAxis.push(0)
      if (item.networks.length > 0 && item.networks[0]) {
        last_send_data = item.networks[0].sent_data
      }
    } else {
      if (item.networks.length > 0 && item.networks[0]) {
        const current_sent_data = item.networks[0].sent_data
        const diff = (current_sent_data - last_send_data) / 1000 / 1000 // MB/s
        if (diff > 0) {
          tmpNetworkSendXAxis.push('')
          tmpNetworkSendYAxis.push(diff)
          networkSendTitle.value = '网络发送速度(' + diff.toFixed(2) + 'MB/s)'
        }
        last_send_data = current_sent_data
      }
    }

    if (last_recv_data === -1) {
      tmpNetworkRecvXAxis.push('')
      tmpNetworkRecvYAxis.push(0)
      if (item.networks.length > 0 && item.networks[0]) {
        last_recv_data = item.networks[0].received_data
      }
    } else {
      if (item.networks.length > 0 && item.networks[0]) {
        const current_recv_data = item.networks[0].received_data
        const diff = (current_recv_data - last_recv_data) / 1000 / 1000 // MB/s
        // if (diff === 0) {
        //   console.log(
        //     'current_recv_data: ',
        //     current_recv_data,
        //     ', last_recv_data: ',
        //     last_recv_data,
        //   )
        // }
        if (diff > 0) {
          tmpNetworkRecvXAxis.push('')
          tmpNetworkRecvYAxis.push(diff)
          networkRecvTitle.value = '网络接收速度(' + diff.toFixed(2) + 'MB/s)'
        }
        last_recv_data = current_recv_data
      }

      if (item.gpus && item.gpus.length > 0) {
        for (const gpuInfo of item.gpus) {
          if (!gpuInfoMap.has(gpuInfo.id)) {
            gpuInfoMap.set(gpuInfo.id, [])
          }
          const sysGpuInfo = gpuInfoMap.get(gpuInfo.id)
          if (sysGpuInfo !== undefined) {
            if (sysGpuInfo.length >= 180) {
              gpuInfoMap.get(gpuInfo.id)?.shift()
            }
          }
          gpuInfoMap.get(gpuInfo.id)?.push(gpuInfo)
        }
      } else {
        gpuInfoMap.clear()
      }
    }
  }
  // cpu usage
  cpuUsageXAxis.value = tmpCpuUsageXAxis
  cpuUsageYAxis.value = tmpCpuUsageYAxis

  // cpu frequency
  cpuFreqXAxis.value = tmpCpuFreqXAxis
  cpuFreqYAxis.value = tmpCpuFreqYAxis

  // mem usage
  const latest_item = sys_info_array.value[sys_info_array.value.length - 1]
  const total_gb = latest_item?.mem.total_gb + 'GB'
  const used_gb = latest_item?.mem.used_gb + 'GB'
  memTitle.value = '内存(已使用' + used_gb + '/总共' + total_gb + ')'
  memUsageXAxis.value = tmpMemUsageXAxis
  memUsageYAxis.value = tmpMemUsageYAxis

  // network send
  networkSendXAxis.value = tmpNetworkSendXAxis
  networkSendYAxis.value = tmpNetworkSendYAxis

  // network receive
  networkRecvXAxis.value = tmpNetworkRecvXAxis
  networkRecvYAxis.value = tmpNetworkRecvYAxis

  // console.log('cpuUsageXAxis: ', cpuUsageXAxis.value)
  // console.log('cpuUsageYAxis: ', cpuUsageYAxis.value)
  // console.log('gpuInfoMap', gpuInfoMap)
}

const diskNormalColor = ref('#409eff')
const diskWarnColor = ref('#f56c6c')

// search device dialog
const searchDialogVisible = ref(false)

// 打开对话框的方法
const openSearchDialog = () => {
  searchDialogVisible.value = true
}

const handleCancel = () => {
  console.log('取消搜索')
  searchDialogVisible.value = false
}

const handleConfirm = () => {
  console.log('确认搜索')
  searchDialogVisible.value = false
}

const handleSelectDevice = (device: Device) => {
  console.log('父组件收到设备:', device)
  if (device === null) {
    return
  }
  if (!device.online) {
    ElNotification({
      message: '此设备: ' + device.device_name + ' 已经离线',
      type: 'warning',
    })
    return
  }
  onlineDevice.value = device
  wsStore.send({
    msg_type: 'stream_hardware_info',
    device_id: onlineDevice.value.device_id,
  })
}
</script>

<template>
  <div>
    <div class="flex flex-col justify-center items-center bg-gray-100 h-52 rounded-lg">
      <div class="text-xl font-semibold text-slate-700">资源信息总览</div>
      <div class="h-5" />
      <div class="flex justify-center">
        <el-card class="w-50 h-30 !rounded-xl" shadow="hover">
          <div class="flex flex-col justify-center items-center">
            <div class="flex font-semibold !text-slate-600 text-base">
              <div class=""><IpDevices /></div>
              <div class="w-1" />
              设备总数
            </div>
            <div class="h-3" />
            <div class="font-semibold text-blue-500 text-3xl">{{ totalDevices }}</div>
          </div>
        </el-card>

        <div class="w-5" />

        <el-card class="w-50 h-30 !rounded-xl" shadow="hover">
          <div class="flex flex-col justify-center items-center">
            <div class="flex font-semibold !text-slate-600 text-base">
              <div class=""><IpConnectionArrow /></div>
              <div class="w-1" />
              在线连接数
            </div>
            <div class="h-3" />
            <div class="font-semibold text-blue-500 text-3xl">{{ totalOnlineConnections }}</div>
          </div>
        </el-card>

        <div class="w-5" />

        <el-card class="w-50 h-30 !rounded-xl" shadow="hover">
          <div class="flex flex-col justify-center items-center">
            <div class="flex font-semibold !text-slate-600 text-base">
              <div class=""><IpPeoplesTwo /></div>
              <div class="w-1" />
              用户数
            </div>
            <div class="h-3" />
            <div class="font-semibold text-blue-500 text-3xl">{{ totalUsers }}</div>
          </div>
        </el-card>

        <div class="w-5" />

        <el-card class="w-50 h-30 !rounded-xl" shadow="hover">
          <div class="flex flex-col justify-center items-center">
            <div class="flex font-semibold !text-slate-600 text-base">
              <div class=""><IpErrorComputer /></div>
              <div class="w-1" />
              事件
            </div>
            <div class="h-3" />
            <div class="font-semibold text-blue-500 text-3xl">{{ totalEvents }}</div>
          </div>
        </el-card>

        <div class="w-5" />

        <el-card class="w-70 h-30 !rounded-xl" shadow="hover">
          <div class="flex flex-col justify-center items-center">
            <div class="flex font-semibold !text-slate-600 text-base">
              <div class=""><IpTimer /></div>
              <div class="w-1" />
              连接总时长
            </div>
            <div class="h-3" />
            <div class="font-semibold text-blue-500 text-3xl">
              {{ formatDuration(totalUsedTime) }}
            </div>
          </div>
        </el-card>
      </div>
    </div>

    <div class="h-5" />

    <DeviceSelectDialog
      v-model="searchDialogVisible"
      title="搜索设备"
      @cancel="handleCancel"
      @confirm="handleConfirm"
      @select="handleSelectDevice"
    />

    <div class="bg-gray-100 rounded-lg pl-5 pt-5 pr-5">
      <div class="text-xl font-semibold pl-1 text-slate-700 flex items-center justify-between">
        基础信息({{
          onlineDevice
            ? '当前设备: ' + onlineDevice.device_name + ', ID: ' + onlineDevice.device_id
            : 'UnKnown'
        }})
        <div class="w-2" />
        <el-button :size="'default'" type="primary" @click="openSearchDialog">选择设备 </el-button>
      </div>

      <div class="h-5" />

      <el-row :gutter="20">
        <el-col :span="8">
          <div class="flex justify-center items-center w-full h-full bg-white rounded-xl">
            <div class="">
              <!--System-->
              <div class="flex justify-start">
                <el-text class="!w-20 font-semibold">OS版本</el-text>
                <div class="w-5"></div>
                <el-text class="w-full">
                  {{ sysInfo?.os.sys_os_long_version + ' - ' + sysInfo?.os.sys_host_name }}</el-text
                >
              </div>

              <!--Uptime-->
              <div class="h-1"></div>
              <div class="flex justify-start">
                <el-text class="!w-20 font-semibold">已开机</el-text>
                <div class="w-5"></div>
                <el-text class="w-full">
                  {{ sysInfo?.uptime }}
                </el-text>
              </div>

              <!--CPU-->
              <div class="h-1"></div>
              <div class="flex justify-start">
                <el-text class="!w-20 font-semibold">CPU型号</el-text>
                <div class="w-5"></div>
                <el-text class="w-full">{{ sysInfo?.cpu.brand }}</el-text>
              </div>

              <div class="h-1"></div>
              <div class="flex justify-start">
                <el-text class="!w-20 font-semibold">CPU参数</el-text>
                <div class="w-5"></div>
                <el-text class="w-full">{{
                  '基准频率: ' +
                  sysInfo?.cpu.base_frequency +
                  ', 核心数: ' +
                  sysInfo?.cpu.cpus.length
                }}</el-text>
              </div>

              <!--Memory-->
              <div class="h-1"></div>
              <div class="flex justify-start">
                <el-text class="w-20 font-semibold">内存</el-text>
                <div class="w-5"></div>
                <el-text class="w-full">{{
                  '已使用: ' + sysInfo?.mem.used_gb + ' GB 总' + sysInfo?.mem.total_gb + ' GB'
                }}</el-text>
              </div>

              <!--Hard Disks-->
              <div class="h-1"></div>
              <div class="flex justify-start">
                <el-text class="w-20 font-semibold">硬盘</el-text>
                <div class="w-5"></div>
                <div class="w-full">
                  <div
                    class="w-full flex"
                    v-for="(disk, index) in sysInfo?.disks"
                    :key="disk.mount_on || index"
                  >
                    <el-progress
                      class="w-40"
                      :percentage="
                        (((disk.total_gb - disk.available_gb) * 100) / disk.total_gb).toFixed(0)
                      "
                      :color="
                        ((disk.total_gb - disk.available_gb) * 100) / disk.total_gb > 50
                          ? diskWarnColor
                          : diskNormalColor
                      "
                    />
                    <div class="text-sm">
                      {{ disk.mount_on.substring(0, 1) }}({{ disk.disk_type }})
                      {{ disk.total_gb - disk.available_gb }}GB/{{ disk.total_gb }} GB
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </el-col>
        <el-col :span="8">
          <LineChart
            title="CPU使用率(%)"
            :show-area="true"
            :x-axis="cpuUsageXAxis"
            :y-axis="cpuUsageYAxis"
            class="!h-80"
          />
        </el-col>
        <el-col :span="8">
          <LineChart
            title="CPU运行频率(GHz)"
            :show-area="true"
            :x-axis="cpuFreqXAxis"
            :y-axis="cpuFreqYAxis"
            class="!h-80"
          />
        </el-col>
      </el-row>
    </div>

    <div class="bg-gray-100 rounded-lg pl-5 pt-5 pr-5 pb-5">
      <el-row :gutter="20">
        <el-col :span="8">
          <LineChart
            :title="memTitle"
            :show-area="true"
            :x-axis="memUsageXAxis"
            :y-axis="memUsageYAxis"
            class="w-min-150 !h-80"
          />
        </el-col>
        <el-col :span="8">
          <LineChart
            :title="networkSendTitle"
            :show-area="true"
            :x-axis="networkSendXAxis"
            :y-axis="networkSendYAxis"
            class="!h-80"
            areaColorStart="rgba(106, 203, 97,0.5)"
            areaColorEnd="rgba(106, 203, 97,0.005)"
          />
        </el-col>
        <el-col :span="8">
          <LineChart
            :title="networkRecvTitle"
            :show-area="true"
            :x-axis="networkRecvXAxis"
            :y-axis="networkRecvYAxis"
            class="!h-80"
            areaColorStart="rgba(255, 128, 0,0.5)"
            areaColorEnd="rgba(255, 128, 0,0.005)"
          />
        </el-col>
      </el-row>
    </div>

    <div class="h-5" />

    <div v-for="(gpuGroup, index) in gpuRenderList" :key="gpuGroup.key" class="">
      <div class="bg-gray-100 rounded-lg pl-5 pt-5 pr-5 pb-5">
        <div class="text-xl font-semibold pl-1 text-slate-700">
          GPU信息({{ index + 1 }} - {{ gpuGroup.brand }})
        </div>
        <div class="h-5" />
        <el-row :gutter="20">
          <el-col :span="6">
            <LineChart
              :title="gpuGroup.usageTitle"
              :show-area="true"
              :x-axis="gpuGroup.usageXAxis"
              :y-axis="gpuGroup.usageYAxis"
              class="!h-80"
              areaColorStart="rgba(75, 150, 255,0.5)"
              areaColorEnd="rgba(75, 150, 255,0.005)"
          /></el-col>
          <el-col :span="6">
            <LineChart
              :title="gpuGroup.encoderTitle"
              :show-area="true"
              :x-axis="gpuGroup.encoderXAxis"
              :y-axis="gpuGroup.encoderYAxis"
              class="!h-80"
              areaColorStart="rgba(75, 150, 255,0.5)"
              areaColorEnd="rgba(75, 150, 255,0.005)"
          /></el-col>
          <el-col :span="6">
            <LineChart
              :title="gpuGroup.memUsageTitle"
              :show-area="true"
              :x-axis="gpuGroup.memUsageXAxis"
              :y-axis="gpuGroup.memUsageYAxis"
              class="!h-80"
              areaColorStart="rgba(75, 150, 255,0.5)"
              areaColorEnd="rgba(75, 150, 255,0.005)"
          /></el-col>
          <el-col :span="6">
            <LineChart
              :title="gpuGroup.tempTitle"
              :show-area="true"
              :x-axis="gpuGroup.tempXAxis"
              :y-axis="gpuGroup.tempYAxis"
              class="!h-80"
              areaColorStart="rgba(75, 150, 255,0.5)"
              areaColorEnd="rgba(75, 150, 255,0.005)"
          /></el-col>
        </el-row>
      </div>
    </div>
  </div>
</template>

<style scoped></style>
