<script setup lang="ts">
import { onMounted, ref } from 'vue'

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
  ElNotification({
    message: '链接拷贝成功',
    type: 'success',
  })
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

import { type ComponentSize, ElNotification } from 'element-plus'
import type { Device } from '@/entity/device.ts'
import { formatTimestamp } from '@/util/time.ts'
import { copyText } from '@/util/clipboard.ts'
import { queryDevices, updateDeviceActive } from '@/model/device_api.ts'
const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<ComponentSize>('default')
const background = ref(false)
const disabled = ref(false)

const handleSizeChange = async (val: number) => {
  pageSize.value = val
  console.log('current page: ', currentPage.value, ' page size: ', pageSize.value)
  devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
}

const handleCurrentChange = async (val: number) => {
  currentPage.value = val
  devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
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
</script>

<template>
  <div class="w-full">
    <el-card class="w-full" shadow="hover">
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备名称</span>
          <div class="h-2" />
          <el-input class="" v-model="searchDeviceName" placeholder="请输入"></el-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备ID</span>
          <div class="h-2" />
          <el-input class="" v-model="searchDeviceCode" placeholder="请输入"></el-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">IP地址</span>
          <div class="h-2" />
          <el-input class="" v-model="searchDeviceIp" placeholder="请输入"></el-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">设备状态</span>
          <div class="h-2" />
          <el-select v-model="searchOnlineState" placeholder="请选择" class="" clearable>
            <el-option
              v-for="item in searchOnlineOptions"
              :key="item.value"
              :label="item.label"
              :value="item.value"
            />
          </el-select>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!h-5"></span>
          <div class="h-2" />
          <el-button class="w-20" type="primary" @click="handleSearchDevices">搜索</el-button>
        </div>
      </div>
    </el-card>

    <div class="h-2" />

    <el-card class="w-full" shadow="never">
      <template #header>
        <div class="">
          <span class="text-lg font-bold text-slate-800">设备列表</span>
        </div>
      </template>

      <el-table :data="devices" style="width: 100%">
        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span class="!font-bold">{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="IP地址" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip_addr }}</span>
          </template>
        </el-table-column>

        <el-table-column label="密码" :min-width="80">
          <template #default="scope">
            <span>{{ scope.row.gen_random_pwd }}</span>
          </template>
        </el-table-column>

        <el-table-column label="使用时长" :min-width="90">
          <template #default="scope">
            <span>{{ scope.row.used_time }}</span>
          </template>
        </el-table-column>

        <el-table-column label="网络状态" :min-width="60">
          <template #default="scope">
            <el-tag :type="scope.row.online ? 'success' : 'danger'" effect="light">
              {{ scope.row.online ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="是否启用" :min-width="60">
          <template #default="scope">
            <el-tag :type="scope.row.active ? 'success' : 'danger'" effect="light">
              {{ scope.row.active ? '已启用' : '已禁用' }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="访问链接" :min-width="100">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="500px">
              <template #default>
                <div>{{ scope.row.desktop_link }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag>{{ scope.row.desktop_link.substring(0, 10) }}...</el-tag>
                  <div class="w-1" />
                  <el-button
                    size="small"
                    class="w-10"
                    @click="handleCopyLink(scope.$index, scope.row)"
                  >
                    复制
                  </el-button>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="登录用户ID" :min-width="100">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="100px">
              <template #default>
                <div>{{ scope.row.logged_in_user_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag>{{
                    scope.row.logged_in_user_id === ''
                      ? '无'
                      : scope.row.logged_in_user_id.substring(0, 10) + '...'
                  }}</el-tag>
                  <div class="w-1" />
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="创建时间" :min-width="100">
          <template #default="scope">
            <span class="!text-small">{{ formatTimestamp(scope.row.created_timestamp) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作" :min-width="200">
          <template #default="scope">
            <el-button size="small" @click="handleHardwareInfo(scope.$index, scope.row)">
              硬件
            </el-button>

            <el-button size="small" @click="handleActiveDevice(scope.$index, scope.row)">
              启用
            </el-button>

            <el-button
              size="small"
              type="danger"
              @click="handleDisableDevice(scope.$index, scope.row)"
            >
              禁用
            </el-button>

<!--            <el-button-->
<!--              size="small"-->
<!--              type="warning"-->
<!--              @click="handleDisableDevice(scope.$index, scope.row)"-->
<!--            >-->
<!--              刷新链接-->
<!--            </el-button>-->

            <!--            <el-button-->
            <!--              size="small"-->
            <!--              type="warning"-->
            <!--              @click="handleFeelessMonitor(scope.$index, scope.row)"-->
            <!--            >-->
            <!--              无感监控-->
            <!--            </el-button>-->
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <div class="h-5" />

    <div class="flex justify-center">
      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        :background="background"
        layout="total, sizes, prev, pager, next, jumper"
        :total="devices.length"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
      />
    </div>
  </div>

  <el-dialog
    v-model="hardwareDialogVisible"
    title="硬件信息"
    :modal="false"
    modal-penetrable
    center
    destroy-on-close
    class="!w-250"
  >
    <!--System-->
    <div class="flex justify-start">
      <el-text class="w-15 font-bold">OS版本</el-text>
      <div class="w-5"></div>
      <el-text class="w-130">
        {{
          currentSelectedDevice?.sys_info.os.sys_os_long_version +
          ' - ' +
          currentSelectedDevice?.sys_info.os.sys_host_name
        }}</el-text
      >
    </div>

    <!--Uptime-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <el-text class="w-15 font-bold">已开机:</el-text>
      <div class="w-5"></div>
      <el-text class="w-130">
        {{ currentSelectedDevice?.sys_info.uptime }}
      </el-text>
    </div>

    <!--CPU-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <el-text class="w-15 font-bold">CPU:</el-text>
      <div class="w-5"></div>
      <el-text class="w-130">{{
        currentSelectedDevice?.sys_info.cpu.brand +
        ' 基准频率: ' +
        currentSelectedDevice?.sys_info.cpu.base_frequency +
        'GHz 核心数: ' +
        currentSelectedDevice?.sys_info.cpu.cpus.length
      }}</el-text>
    </div>

    <!--Memory-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <el-text class="w-15 font-bold">内存:</el-text>
      <div class="w-5"></div>
      <el-text class="w-130">{{
        '已使用: ' +
        currentSelectedDevice?.sys_info.mem.used_gb +
        ' GB 总' +
        currentSelectedDevice?.sys_info.mem.total_gb +
        ' GB'
      }}</el-text>
    </div>

    <!--Hard Disks-->
    <div class="h-1"></div>
    <div class="flex justify-start">
      <el-text class="w-15 font-bold">硬盘:</el-text>
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
      <el-text class="w-15 font-bold">GPU:</el-text>
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
    <template #footer>
      <div class="dialog-footer">
        <el-button type="primary" @click="hardwareDialogVisible = false"> 关闭 </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped>
/* 关键 1：真正控制 select 高度的地方 */
.h-9-select :deep(.el-select__wrapper) {
  height: 36px !important;
  min-height: 36px !important;
}

/* 关键 2：里面的 input */
.h-9-select :deep(.el-input__wrapper) {
  height: 36px !important;
  min-height: 36px !important;
  padding-top: 0;
  padding-bottom: 0;
}

.h-9-select :deep(.el-input__inner) {
  line-height: 36px;
}
</style>
