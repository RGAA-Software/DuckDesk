<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import { notification } from 'ant-design-vue'
import { useWsStore } from '@/stores/ws.ts'
import {
  applyDeviceOnlineStateChanged,
  parseDeviceOnlineStateChanged,
} from '@/model/device_online_state.ts'

const router = useRouter()
const wsStore = useWsStore()

const hardwareDialogVisible = ref(false)
const currentSelectedDevice = ref<Device>()

const handleHardwareInfo = (index: number, device: Device) => {
  console.log(index, device)
  currentSelectedDevice.value = device
  hardwareDialogVisible.value = true
}

const handleActiveDevice = async (index: number, device: Device) => {
  console.log('active device: ', index, device)
  if (await updateDeviceActive(device, true)) {
    devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
  }
}

const handleDisableDevice = async (index: number, device: Device) => {
  console.log('disable device: ', index, device)
  if (await updateDeviceActive(device, false)) {
    devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
  }
}

// const handleFeelessMonitor = (index: number, device: Device) => {
//   console.log('monitor device: ', index, device)
// }

const handleCopyLink = async (index: number, device: Device) => {
  console.log('copy link: ', index, device)
  await copyText(device.desktop_link)
  notification.success({
    message: '链接拷贝成功',
  })
}

// 打开 render 托管的 Web 桌面(WebRTC 局域网直连页面)。
// desktop_link: link://base64(json{did, ips[], rdpt, rpwd, ...})
// web 入口用 ?c= URL-safe Base64,避免明文 deviceId/password
const handleOpenWebDesktop = (index: number, device: Device) => {
  try {
    const raw = device.desktop_link.startsWith('link://')
      ? device.desktop_link.substring(7)
      : device.desktop_link
    const info = JSON.parse(atob(raw))
    // ips 元素可能是字符串,也可能是 {ip: "..."} 结构
    const first = info.ips?.[0]
    const ip = typeof first === 'string' ? first : first?.ip
    const port = info.rdpt
    if (!ip || !port) {
      notification.error({ message: '该设备的链接缺少 IP 或端口信息' })
      return
    }
    const did = info.did || device.device_id
    const password = typeof info.rpwd === 'string' ? info.rpwd : ''
    window.open(
      buildWebClientUrl(ip, port, { deviceId: did, password }),
      '_blank',
      'noopener,noreferrer',
    )
  } catch (e) {
    console.error(e)
    notification.error({ message: '解析设备链接失败' })
  }
}

// 跳转到设备录像查看页（/records/:device_id）
const handleOpenRecords = (index: number, device: Device) => {
  console.log('open records: ', index, device.device_id)
  router.push(`/records/${device.device_id}`)
}

const devices = ref<Device[]>([])
// const tableRowClassName = ({ row, rowIndex }: { row: Device; rowIndex: number }) => {
//   console.log('===> rowIndex: ', rowIndex, row)
//   if (rowIndex === 1) {
//     return 'warning-row'
//   } else if (rowIndex === 3) {
//     return 'success-row'
//   }
//   return ''
// }

import type { Device } from '@/entity/device.ts'
import { formatTimestamp } from '@/util/time.ts'
import { copyText } from '@/util/clipboard.ts'
import { buildWebClientUrl } from '@/util/web_client_url.ts'
import { queryDevices, updateDeviceActive } from '@/model/device_api.ts'
const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<'small' | 'default'>('default')
const disabled = ref(false)

const handlePageChange = async (page: number, size: number) => {
  currentPage.value = page
  pageSize.value = size
  devices.value = await queryDevices(
    searchDeviceName.value,
    searchDeviceCode.value,
    searchDeviceIp.value,
    searchOnlineState.value,
    page,
    size,
  )
}

// onMounted
onMounted(async () => {
  devices.value = await queryDevices('', '', '', '', 1, 20)
})

// search the devices
const searchDeviceName = ref<string>('')
const searchDeviceCode = ref<string>('')
const searchDeviceIp = ref<string>('')
const searchOnlineState = ref<string>('')
const searchOnlineOptions = [
  {
    value: 'all',
    label: '全部',
  },
  {
    value: 'online',
    label: '在线',
  },
  {
    value: 'offline',
    label: '离线',
  },
]

const handleSearchDevices = async () => {
  console.log(
    'search, device name: ',
    searchDeviceName.value,
    ', device id: ',
    searchDeviceCode.value,
    ', ip: ',
    searchDeviceIp.value,
    ', online stat: ',
    searchOnlineState.value,
  )
  devices.value = await queryDevices(
    searchDeviceName.value,
    searchDeviceCode.value,
    searchDeviceIp.value,
    searchOnlineState.value,
    currentPage.value,
    pageSize.value,
  )
}

watch(
  () => wsStore.message,
  async (message) => {
    const event = parseDeviceOnlineStateChanged(message)
    if (!event) return

    // A filtered table may need to add or remove the row entirely. Requery in
    // that case; the unfiltered table can update the visible row immediately.
    if (searchOnlineState.value === 'online' || searchOnlineState.value === 'offline') {
      await handleSearchDevices()
      return
    }
    applyDeviceOnlineStateChanged(devices.value, event)
  },
)
</script>

<template>
  <div class="w-full">
    <a-card class="w-full" hoverable>
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备名称</span>
          <div class="h-2" />
          <a-input
            class=""
            v-model:value="searchDeviceName"
            placeholder="请输入"
            @pressEnter="handleSearchDevices"
          />
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备ID</span>
          <div class="h-2" />
          <a-input
            class=""
            v-model:value="searchDeviceCode"
            placeholder="请输入"
            @pressEnter="handleSearchDevices"
          />
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">IP地址</span>
          <div class="h-2" />
          <a-input
            class=""
            v-model:value="searchDeviceIp"
            placeholder="请输入"
            @pressEnter="handleSearchDevices"
          />
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备状态</span>
          <div class="h-2" />
          <a-select
            v-model:value="searchOnlineState"
            placeholder="请选择"
            class="w-full"
            allowClear
          >
            <a-select-option v-for="item in searchOnlineOptions" :key="item.value" :value="item.value">
              {{ item.label }}
            </a-select-option>
          </a-select>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!h-5"></span>
          <div class="h-2" />
          <a-button class="w-20" type="primary" @click="handleSearchDevices">搜索</a-button>
        </div>
      </div>
    </a-card>

    <div class="h-2" />

    <a-card class="w-full" :bordered="false">
      <template #title>
        <div class="">
          <span class="text-lg font-bold text-slate-800">设备列表</span>
        </div>
      </template>

      <a-table :data-source="devices" style="width: 100%" row-key="device_id">
        <a-table-column title="设备ID" :min-width="80">
          <template #default="{ record }">
            <span class="!font-bold">{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :min-width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="IP地址" :min-width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip_addr }}</span>
          </template>
        </a-table-column>

        <a-table-column title="密码" :min-width="80">
          <template #default="{ record }">
            <span>{{ record.gen_random_pwd }}</span>
          </template>
        </a-table-column>

        <a-table-column title="使用时长" :min-width="90">
          <template #default="{ record }">
            <span>{{ record.used_time }}</span>
          </template>
        </a-table-column>

        <a-table-column title="网络状态" :min-width="60">
          <template #default="{ record }">
            <a-tag :color="record.online ? 'success' : 'error'">
              {{ record.online ? '在线' : '离线' }}
            </a-tag>
          </template>
        </a-table-column>

        <a-table-column title="是否启用" :min-width="60">
          <template #default="{ record }">
            <a-tag :color="record.active ? 'success' : 'error'">
              {{ record.active ? '已启用' : '已禁用' }}
            </a-tag>
          </template>
        </a-table-column>

        <a-table-column title="访问链接" :min-width="100">
          <template #default="{ record, index }">
            <a-popover
              trigger="hover"
              placement="top"
              :overlay-inner-style="{ width: '500px' }"
            >
              <template #content>
                <div>{{ record.desktop_link }}</div>
              </template>
              <div class="flex">
                <a-tag>{{ record.desktop_link.substring(0, 10) }}...</a-tag>
                <div class="w-1" />
                <a-button size="small" class="w-10" @click="handleCopyLink(index, record)">
                  复制
                </a-button>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="登录用户ID" :min-width="100">
          <template #default="{ record }">
            <a-popover
              trigger="hover"
              placement="top"
              :overlay-inner-style="{ width: '100px' }"
            >
              <template #content>
                <div>{{ record.logged_in_user_id }}</div>
              </template>
              <div class="flex">
                <a-tag>{{
                  record.logged_in_user_id === ''
                    ? '无'
                    : record.logged_in_user_id.substring(0, 10) + '...'
                }}</a-tag>
                <div class="w-1" />
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="创建时间" :min-width="100">
          <template #default="{ record }">
            <span class="!text-small">{{ formatTimestamp(record.created_timestamp) }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作" :min-width="200">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleHardwareInfo(index, record)">
              硬件
            </a-button>

            <a-button size="small" @click="handleOpenRecords(index, record)">
              录像
            </a-button>

            <a-button
              size="small"
              type="primary"
              @click="handleOpenWebDesktop(index, record)"
            >
              Web桌面
            </a-button>

            <a-button size="small" @click="handleActiveDevice(index, record)">
              启用
            </a-button>

            <a-button
              size="small"
              type="danger"
              @click="handleDisableDevice(index, record)"
            >
              禁用
            </a-button>

<!--            <a-button-->
<!--              size="small"-->
<!--              type="default"-->
<!--              @click="handleDisableDevice(index, record)"-->
<!--            >-->
<!--              刷新链接-->
<!--            </a-button>-->

            <!--            <a-button-->
            <!--              size="small"-->
            <!--              type="default"-->
            <!--              @click="handleFeelessMonitor(index, record)"-->
            <!--            >-->
            <!--              无感监控-->
            <!--            </a-button>-->
          </template>
        </a-table-column>
      </a-table>
    </a-card>

    <div class="h-5" />

    <div class="flex justify-center">
      <a-pagination
        v-model:current="currentPage"
        v-model:page-size="pageSize"
        :page-size-options="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        show-size-changer
        :total="devices.length"
        @change="handlePageChange"
      />
    </div>
  </div>

  <a-modal
    v-model:open="hardwareDialogVisible"
    title="硬件信息"
    :mask="false"
    centered
    destroy-on-close
    class="!w-250"
    :footer="null"
  >
    <!--System-->
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">OS版本</a-typography-text>
      <div class="w-5"></div>
      <a-typography-text class="w-130">
        {{
          currentSelectedDevice?.sys_info.os.sys_os_long_version +
          ' - ' +
          currentSelectedDevice?.sys_info.os.sys_host_name
        }}</a-typography-text
      >
    </div>

    <!--Uptime-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">已开机:</a-typography-text>
      <div class="w-5"></div>
      <a-typography-text class="w-130">
        {{ currentSelectedDevice?.sys_info.uptime }}
      </a-typography-text>
    </div>

    <!--CPU-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">CPU:</a-typography-text>
      <div class="w-5"></div>
      <a-typography-text class="w-130">{{
        currentSelectedDevice?.sys_info.cpu.brand +
        ' 基准频率: ' +
        currentSelectedDevice?.sys_info.cpu.base_frequency +
        'GHz 核心数: ' +
        currentSelectedDevice?.sys_info.cpu.cpus.length
      }}</a-typography-text>
    </div>

    <!--Memory-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">内存:</a-typography-text>
      <div class="w-5"></div>
      <a-typography-text class="w-130">{{
        '已使用: ' +
        currentSelectedDevice?.sys_info.mem.used_gb +
        ' GB 总' +
        currentSelectedDevice?.sys_info.mem.total_gb +
        ' GB'
      }}</a-typography-text>
    </div>

    <!--Hard Disks-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">硬盘:</a-typography-text>
      <div class="w-5"></div>
      <div>
        <div
          class=""
          v-for="(disk, index) in currentSelectedDevice?.sys_info.disks"
          :key="disk.mount_on || index"
        >
          <div>
            >
            {{ index + 1 }}
            盘符：{{ disk.mount_on }} - 类型：{{ disk.disk_type }} - 文件系统：{{
              disk.filesystem
            }}
            - 总容量：{{ disk.total_gb }} GB - 可用空间：{{ disk.available_gb }} GB
          </div>
        </div>
      </div>
    </div>

    <!--GPUS-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <a-typography-text class="w-15 font-bold">GPU:</a-typography-text>
      <div class="w-5"></div>
      <div>
        <div v-for="(gpu, index) in currentSelectedDevice?.sys_info.gpus" :key="gpu.id || index">
          <div>
            >
            {{ index + 1 }}
            GPU：{{ gpu.brand }} - 核心占用：{{ gpu.gpu_utilization }}% - 显存占用：{{
              gpu.mem_utilization
            }}% - 温度：{{ gpu.temperature }}℃ - 显存：{{ gpu.mem_used_gb }} /
            {{ gpu.mem_total_gb }} GB
          </div>
        </div>
      </div>
    </div>

    <div class="h-5"></div>
    <div class="flex justify-end">
      <a-button type="primary" @click="hardwareDialogVisible = false"> 关闭 </a-button>
    </div>
  </a-modal>
</template>

<style scoped></style>
