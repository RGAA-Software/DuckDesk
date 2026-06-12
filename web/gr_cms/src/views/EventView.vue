<script setup lang="ts">
import { SpvrEvent } from '@/entity/spvr_event.ts'
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import { IpCpu, IpMemory, IpDisk, IpMultiTriangularFour } from 'vue-icons-plus/ip'
import { copyText } from '@/util/clipboard.ts'
import { ElNotification } from 'element-plus'

// query events
async function queryEvents(
  page: number,
  pageSize: number,
  eventType: string,
  deviceId: string,
  deviceName: string,
  deviceIp: string,
) {
  const resp = await axiosHttp.get('/api/v1/event/control/query', {
    params: {
      page: page,
      page_size: pageSize,
      appkey: localStorage.getItem('appkey'),
      sort_time: -1,
      event_type: eventType,
      device_id: deviceId,
      device_name: deviceName,
      device_ip: deviceIp,
    },
  })
  if (resp.status !== 200) {
    console.error('queryVisits failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryVisits failed, data:', data)
    return
  }

  console.log('will query: ', eventType)
  if (eventType === 'cpu') {
    cpuEvents.value = data.data
    if (cpuEvents.value.length > 0 && cpuEvents.value[0] !== null) {
      totalCpuSize.value = cpuEvents.value[0]!.total
    }
    console.log('CPU, total: ', totalCpuSize.value, ', events: ', cpuEvents.value)
  } else if (eventType === 'memory') {
    memoryEvents.value = data.data
    if (memoryEvents.value.length > 0 && memoryEvents.value[0] !== null) {
      totalMemorySize.value = memoryEvents.value[0]!.total
    }
    console.log('Memory, total: ', totalMemorySize.value, ', events: ', memoryEvents.value)
  } else if (eventType === 'disk') {
    diskEvents.value = data.data
    if (diskEvents.value.length > 0 && diskEvents.value[0] !== null) {
      totalDiskSize.value = diskEvents.value[0]!.total
    }
    console.log('Disk, total: ', totalDiskSize.value, ', events: ', diskEvents.value)
  } else if (eventType === 'gpu') {
    gpuEvents.value = data.data
    if (gpuEvents.value.length > 0 && gpuEvents.value[0] !== null) {
      totalGpuSize.value = gpuEvents.value[0]!.total
    }
    console.log('Gpu, total: ', totalGpuSize.value, ', events: ', gpuEvents.value)
  }
}

// CPU
const cpuEvents = ref<SpvrEvent[]>([])
const cpuPageSize = ref(20)
const cpuCurrentPage = ref(1)
const totalCpuSize = ref(0)
const handleSizeChange = (val: number) => {
  cpuPageSize.value = val
  console.log('current page: ', cpuCurrentPage.value, ' page size: ', cpuPageSize.value)
  queryEvents(cpuCurrentPage.value, cpuPageSize.value, 'cpu', '', '', '')
}

const handleCurrentChange = (val: number) => {
  cpuCurrentPage.value = val
  queryEvents(cpuCurrentPage.value, cpuPageSize.value, 'cpu', '', '', '')
}

// Memory
const memoryEvents = ref<SpvrEvent[]>([])
const memoryPageSize = ref(20)
const memoryCurrentPage = ref(1)
const totalMemorySize = ref(0)
const handleMemorySizeChange = (val: number) => {
  memoryPageSize.value = val
  console.log('current page: ', memoryCurrentPage.value, ' page size: ', memoryPageSize.value)
  queryEvents(memoryCurrentPage.value, memoryPageSize.value, 'memory', '', '', '')
}

const handleMemoryCurrentChange = (val: number) => {
  memoryCurrentPage.value = val
  queryEvents(memoryCurrentPage.value, memoryPageSize.value, 'memory', '', '', '')
}

// disk
const diskEvents = ref<SpvrEvent[]>([])
const diskPageSize = ref(20)
const diskCurrentPage = ref(1)
const totalDiskSize = ref(0)
const handleDiskSizeChange = (val: number) => {
  diskPageSize.value = val
  console.log('current page: ', diskCurrentPage.value, ' page size: ', diskPageSize.value)
  queryEvents(diskCurrentPage.value, diskPageSize.value, 'disk', '', '', '')
}

const handleDiskCurrentChange = (val: number) => {
  diskCurrentPage.value = val
  queryEvents(diskCurrentPage.value, diskPageSize.value, 'disk', '', '', '')
}

// GPU
const gpuEvents = ref<SpvrEvent[]>([])
const gpuPageSize = ref(20)
const gpuCurrentPage = ref(1)
const totalGpuSize = ref(0)
const handleGpuSizeChange = (val: number) => {
  gpuPageSize.value = val
  console.log('current page: ', gpuCurrentPage.value, ' page size: ', gpuPageSize.value)
  queryEvents(gpuCurrentPage.value, gpuPageSize.value, 'gpu', '', '', '')
}

const handleGpuCurrentChange = (val: number) => {
  gpuCurrentPage.value = val
  queryEvents(gpuCurrentPage.value, gpuPageSize.value, 'gpu', '', '', '')
}

// copy
const handleCopyEvent = async (index: number, event: SpvrEvent) => {
  console.log('copy event: ', index)
  await copyText(JSON.stringify(event))
  ElNotification({
    title: '复制成功',
    type: 'success',
  })
}

const queryAllEvents = async (deviceId: string, deviceName: string, deviceIp: string) => {
  // cpu
  await queryEvents(cpuCurrentPage.value, cpuPageSize.value, 'cpu', deviceId, deviceName, deviceIp)

  // memory
  await queryEvents(
    memoryCurrentPage.value,
    memoryPageSize.value,
    'memory',
    deviceId,
    deviceName,
    deviceIp,
  )

  // disk
  await queryEvents(
    diskCurrentPage.value,
    diskPageSize.value,
    'disk',
    deviceId,
    deviceName,
    deviceIp,
  )

  // gpu
  await queryEvents(gpuCurrentPage.value, gpuPageSize.value, 'gpu', deviceId, deviceName, deviceIp)
}

onMounted(async () => {
  await queryAllEvents('', '', '')
})

//
const searchDeviceId = ref<string>('')
const searchDeviceName = ref<string>('')
const searchDeviceIp = ref<string>('')

const handleSearch = async () => {
  await queryAllEvents(searchDeviceId.value, searchDeviceName.value, searchDeviceIp.value)
}

</script>

<template>
  <el-card class="w-full" shadow="hover">
    <div class="flex">
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备ID</span>
        <div class="h-2" />
        <el-input class="" v-model="searchDeviceId" placeholder="请输入"></el-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备名称</span>
        <div class="h-2" />
        <el-input class="" v-model="searchDeviceName" placeholder="请输入"></el-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备IP地址</span>
        <div class="h-2" />
        <el-input class="" v-model="searchDeviceIp" placeholder="请输入"></el-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!h-5"></span>
        <div class="h-2" />
        <el-button class="w-20" type="primary" @click="handleSearch">搜索</el-button>
      </div>
    </div>
  </el-card>

  <div class="h-2" />

  <el-tabs class="custom-tabs">
    <!--       CPU        -->
    <!--       CPU        -->
    <!--       CPU        -->
    <el-tab-pane>
      <template #label>
        <span class="custom-tabs-label">
          <el-icon><IpCpu /></el-icon>
          <span>CPU</span>
        </span>
      </template>

      <el-table :data="cpuEvents" style="width: 100%">
        <el-table-column label="事件ID" :min-width="40">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="100px">
              <template #default>
                <div>{{ scope.row.event_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info">{{ scope.row.event_id.substring(0, 10) }}...</el-tag>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span>{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备IP" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip }}</span>
          </template>
        </el-table-column>

        <el-table-column label="用户ID" :min-width="80">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="200px">
              <template #default>
                <div>{{ scope.row.user_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info"
                    >{{
                      scope.row.user_id.length > 20 ? scope.row.user_id.substring(0, 20) : ''
                    }}...</el-tag
                  >
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="使用率" :min-width="80">
          <template #default="scope">
            <span class="!font-bold text-amber-600">{{ scope.row.cpu_usage }}%</span>
          </template>
        </el-table-column>

        <el-table-column label="上报时间" :min-width="120">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.readable_timestamp }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作">
          <template #default="scope">
            <el-button size="small" @click="handleCopyEvent(scope.$index, scope.row)">
              复制
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="h-5" />
      <div class="flex justify-center">
        <el-pagination
          v-model:current-page="cpuCurrentPage"
          v-model:page-size="cpuPageSize"
          :page-sizes="[20, 40, 60, 80]"
          layout="total, sizes, prev, pager, next, jumper"
          :total="totalCpuSize"
          @size-change="handleSizeChange"
          @current-change="handleCurrentChange"
        />
      </div>
      <div class="h-5" />
    </el-tab-pane>

    <!--       Memory        -->
    <!--       Memory        -->
    <!--       Memory        -->
    <el-tab-pane>
      <template #label>
        <span class="custom-tabs-label">
          <el-icon><IpMemory /></el-icon>
          <span>内存</span>
        </span>
      </template>

      <el-table :data="memoryEvents" style="width: 100%">
        <el-table-column label="事件ID" :min-width="40">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="100px">
              <template #default>
                <div>{{ scope.row.event_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info">{{ scope.row.event_id.substring(0, 10) }}...</el-tag>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span>{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备IP" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip }}</span>
          </template>
        </el-table-column>

        <el-table-column label="用户ID" :min-width="80">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="200px">
              <template #default>
                <div>{{ scope.row.user_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info"
                    >{{
                      scope.row.user_id.length > 20 ? scope.row.user_id.substring(0, 20) : ''
                    }}...</el-tag
                  >
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="使用率" :min-width="80">
          <template #default="scope">
            <span class="!font-bold text-amber-600">{{ scope.row.mem_usage }}%</span>
          </template>
        </el-table-column>

        <el-table-column label="上报时间" :min-width="120">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.readable_timestamp }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作">
          <template #default="scope">
            <el-button size="small" @click="handleCopyEvent(scope.$index, scope.row)">
              复制
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <el-pagination
          v-model:current-page="memoryCurrentPage"
          v-model:page-size="memoryPageSize"
          :page-sizes="[20, 40, 60, 80]"
          layout="total, sizes, prev, pager, next, jumper"
          :total="totalMemorySize"
          @size-change="handleMemorySizeChange"
          @current-change="handleMemoryCurrentChange"
        />
      </div>
      <div class="h-5" />
    </el-tab-pane>

    <!--       HDisk        -->
    <!--       HDisk        -->
    <!--       HDisk        -->
    <el-tab-pane>
      <template #label>
        <span class="custom-tabs-label">
          <el-icon><IpDisk /></el-icon>
          <span>硬盘</span>
        </span>
      </template>

      <el-table :data="diskEvents" style="width: 100%">
        <el-table-column label="事件ID" :min-width="40">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="100px">
              <template #default>
                <div>{{ scope.row.event_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info">{{ scope.row.event_id.substring(0, 10) }}...</el-tag>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span>{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备IP" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip }}</span>
          </template>
        </el-table-column>

        <el-table-column label="用户ID" :min-width="80">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="200px">
              <template #default>
                <div>{{ scope.row.user_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info"
                    >{{
                      scope.row.user_id.length > 20 ? scope.row.user_id.substring(0, 20) : ''
                    }}...</el-tag
                  >
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="磁盘" :min-width="40">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.disk_path }}</span>
          </template>
        </el-table-column>

        <el-table-column label="使用率" :min-width="80">
          <template #default="scope">
            <span class="!font-bold text-amber-600">{{ scope.row.disk_usage }}%</span>
          </template>
        </el-table-column>

        <el-table-column label="上报时间" :min-width="120">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.readable_timestamp }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作">
          <template #default="scope">
            <el-button size="small" @click="handleCopyEvent(scope.$index, scope.row)">
              复制
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <el-pagination
          v-model:current-page="diskCurrentPage"
          v-model:page-size="diskPageSize"
          :page-sizes="[20, 40, 60, 80]"
          layout="total, sizes, prev, pager, next, jumper"
          :total="totalDiskSize"
          @size-change="handleDiskSizeChange"
          @current-change="handleDiskCurrentChange"
        />
      </div>
      <div class="h-5" />
    </el-tab-pane>

    <!--       GPU        -->
    <!--       GPU        -->
    <!--       GPU        -->
    <el-tab-pane>
      <template #label>
        <span class="custom-tabs-label">
          <el-icon><IpMultiTriangularFour /></el-icon>
          <span>GPU(显卡)</span>
        </span>
      </template>

      <el-table :data="gpuEvents" style="width: 100%">
        <el-table-column label="事件ID" :min-width="40">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="100px">
              <template #default>
                <div>{{ scope.row.event_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info">{{ scope.row.event_id.substring(0, 10) }}...</el-tag>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span>{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备IP" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip }}</span>
          </template>
        </el-table-column>

        <el-table-column label="用户ID" :min-width="80">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="200px">
              <template #default>
                <div>{{ scope.row.user_id }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag type="info"
                    >{{
                      scope.row.user_id.length > 20 ? scope.row.user_id.substring(0, 20) : ''
                    }}...</el-tag
                  >
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="GPU名称" :min-width="140">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.gpu_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="使用率" :min-width="80">
          <template #default="scope">
            <span class="!font-bold text-amber-600">{{ scope.row.gpu_usage }}%</span>
          </template>
        </el-table-column>

        <el-table-column label="上报时间" :min-width="120">
          <template #default="scope">
            <span class="!font-semibold">{{ scope.row.readable_timestamp }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作">
          <template #default="scope">
            <el-button size="small" @click="handleCopyEvent(scope.$index, scope.row)">
              复制
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <el-pagination
          v-model:current-page="gpuCurrentPage"
          v-model:page-size="gpuPageSize"
          :page-sizes="[20, 40, 60, 80]"
          layout="total, sizes, prev, pager, next, jumper"
          :total="totalGpuSize"
          @size-change="handleGpuSizeChange"
          @current-change="handleGpuCurrentChange"
        />
      </div>
      <div class="h-5" />
    </el-tab-pane>
  </el-tabs>
</template>

<style scoped>
.custom-tabs > .el-tabs__content {
  padding: 32px;
  color: #6b778c;
  font-size: 32px;
  font-weight: 600;
}
.custom-tabs .custom-tabs-label .el-icon {
  vertical-align: middle;
}
.custom-tabs .custom-tabs-label span {
  vertical-align: middle;
  margin-left: 4px;
}
</style>
