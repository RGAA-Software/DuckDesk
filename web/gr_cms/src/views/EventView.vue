<script setup lang="ts">
import { SpvrEvent } from '@/entity/spvr_event.ts'
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import { FundOutlined, DatabaseOutlined, HddOutlined, ThunderboltOutlined } from '@ant-design/icons-vue'
import { copyText } from '@/util/clipboard.ts'
import { notification } from 'ant-design-vue'

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
const handleCpuPageChange = (page: number, pageSize: number) => {
  cpuCurrentPage.value = page
  cpuPageSize.value = pageSize
  queryEvents(page, pageSize, 'cpu', '', '', '')
}

// Memory
const memoryEvents = ref<SpvrEvent[]>([])
const memoryPageSize = ref(20)
const memoryCurrentPage = ref(1)
const totalMemorySize = ref(0)
const handleMemoryPageChange = (page: number, pageSize: number) => {
  memoryCurrentPage.value = page
  memoryPageSize.value = pageSize
  queryEvents(page, pageSize, 'memory', '', '', '')
}

// disk
const diskEvents = ref<SpvrEvent[]>([])
const diskPageSize = ref(20)
const diskCurrentPage = ref(1)
const totalDiskSize = ref(0)
const handleDiskPageChange = (page: number, pageSize: number) => {
  diskCurrentPage.value = page
  diskPageSize.value = pageSize
  queryEvents(page, pageSize, 'disk', '', '', '')
}

// GPU
const gpuEvents = ref<SpvrEvent[]>([])
const gpuPageSize = ref(20)
const gpuCurrentPage = ref(1)
const totalGpuSize = ref(0)
const handleGpuPageChange = (page: number, pageSize: number) => {
  gpuCurrentPage.value = page
  gpuPageSize.value = pageSize
  queryEvents(page, pageSize, 'gpu', '', '', '')
}

// copy
const handleCopyEvent = async (index: number, event: SpvrEvent) => {
  console.log('copy event: ', index)
  await copyText(JSON.stringify(event))
  notification.success({
    message: '复制成功',
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
  <a-card class="w-full" hoverable>
    <div class="flex">
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备ID</span>
        <div class="h-2" />
        <a-input class="" v-model:value="searchDeviceId" placeholder="请输入"></a-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备名称</span>
        <div class="h-2" />
        <a-input class="" v-model:value="searchDeviceName" placeholder="请输入"></a-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!text-sm">设备IP地址</span>
        <div class="h-2" />
        <a-input class="" v-model:value="searchDeviceIp" placeholder="请输入"></a-input>
      </div>

      <div class="w-5" />
      <div class="w-40 flex flex-col items-start">
        <span class="!h-5"></span>
        <div class="h-2" />
        <a-button class="w-20" type="primary" @click="handleSearch">搜索</a-button>
      </div>
    </div>
  </a-card>

  <div class="h-2" />

  <a-tabs class="custom-tabs">
    <!--       CPU        -->
    <!--       CPU        -->
    <!--       CPU        -->
    <a-tab-pane key="cpu">
      <template #tab>
        <span class="custom-tabs-label">
          <FundOutlined />
          <span>CPU</span>
        </span>
      </template>

      <a-table :data-source="cpuEvents" row-key="event_id" style="width: 100%">
        <a-table-column title="事件ID" :width="40">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="100">
              <template #content>
                <div>{{ record.event_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">{{ record.event_id.substring(0, 10) }}...</a-tag>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="设备ID" :width="80">
          <template #default="{ record }">
            <span>{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备IP" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip }}</span>
          </template>
        </a-table-column>

        <a-table-column title="用户ID" :width="80">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="200">
              <template #content>
                <div>{{ record.user_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">
                  {{
                    record.user_id.length > 20 ? record.user_id.substring(0, 20) : ''
                  }}...</a-tag
                >
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="使用率" :width="80">
          <template #default="{ record }">
            <span class="!font-bold text-amber-600">{{ record.cpu_usage }}%</span>
          </template>
        </a-table-column>

        <a-table-column title="上报时间" :width="120">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.readable_timestamp }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleCopyEvent(index, record)">
              复制
            </a-button>
          </template>
        </a-table-column>
      </a-table>

      <div class="h-5" />
      <div class="flex justify-center">
        <a-pagination
          v-model:current="cpuCurrentPage"
          v-model:page-size="cpuPageSize"
          :page-size-options="[20, 40, 60, 80]"
          :total="totalCpuSize"
          show-size-changer
          @change="handleCpuPageChange"
        />
      </div>
      <div class="h-5" />
    </a-tab-pane>

    <!--       Memory        -->
    <!--       Memory        -->
    <!--       Memory        -->
    <a-tab-pane key="memory">
      <template #tab>
        <span class="custom-tabs-label">
          <DatabaseOutlined />
          <span>内存</span>
        </span>
      </template>

      <a-table :data-source="memoryEvents" row-key="event_id" style="width: 100%">
        <a-table-column title="事件ID" :width="40">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="100">
              <template #content>
                <div>{{ record.event_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">{{ record.event_id.substring(0, 10) }}...</a-tag>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="设备ID" :width="80">
          <template #default="{ record }">
            <span>{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备IP" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip }}</span>
          </template>
        </a-table-column>

        <a-table-column title="用户ID" :width="80">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="200">
              <template #content>
                <div>{{ record.user_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">
                  {{
                    record.user_id.length > 20 ? record.user_id.substring(0, 20) : ''
                  }}...</a-tag
                >
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="使用率" :width="80">
          <template #default="{ record }">
            <span class="!font-bold text-amber-600">{{ record.mem_usage }}%</span>
          </template>
        </a-table-column>

        <a-table-column title="上报时间" :width="120">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.readable_timestamp }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleCopyEvent(index, record)">
              复制
            </a-button>
          </template>
        </a-table-column>
      </a-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <a-pagination
          v-model:current="memoryCurrentPage"
          v-model:page-size="memoryPageSize"
          :page-size-options="[20, 40, 60, 80]"
          :total="totalMemorySize"
          show-size-changer
          @change="handleMemoryPageChange"
        />
      </div>
      <div class="h-5" />
    </a-tab-pane>

    <!--       HDisk        -->
    <!--       HDisk        -->
    <!--       HDisk        -->
    <a-tab-pane key="disk">
      <template #tab>
        <span class="custom-tabs-label">
          <HddOutlined />
          <span>硬盘</span>
        </span>
      </template>

      <a-table :data-source="diskEvents" row-key="event_id" style="width: 100%">
        <a-table-column title="事件ID" :width="40">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="100">
              <template #content>
                <div>{{ record.event_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">{{ record.event_id.substring(0, 10) }}...</a-tag>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="设备ID" :width="80">
          <template #default="{ record }">
            <span>{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备IP" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip }}</span>
          </template>
        </a-table-column>

        <a-table-column title="用户ID" :width="80">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="200">
              <template #content>
                <div>{{ record.user_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">
                  {{
                    record.user_id.length > 20 ? record.user_id.substring(0, 20) : ''
                  }}...</a-tag
                >
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="磁盘" :width="40">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.disk_path }}</span>
          </template>
        </a-table-column>

        <a-table-column title="使用率" :width="80">
          <template #default="{ record }">
            <span class="!font-bold text-amber-600">{{ record.disk_usage }}%</span>
          </template>
        </a-table-column>

        <a-table-column title="上报时间" :width="120">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.readable_timestamp }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleCopyEvent(index, record)">
              复制
            </a-button>
          </template>
        </a-table-column>
      </a-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <a-pagination
          v-model:current="diskCurrentPage"
          v-model:page-size="diskPageSize"
          :page-size-options="[20, 40, 60, 80]"
          :total="totalDiskSize"
          show-size-changer
          @change="handleDiskPageChange"
        />
      </div>
      <div class="h-5" />
    </a-tab-pane>

    <!--       GPU        -->
    <!--       GPU        -->
    <!--       GPU        -->
    <a-tab-pane key="gpu">
      <template #tab>
        <span class="custom-tabs-label">
          <ThunderboltOutlined />
          <span>GPU(显卡)</span>
        </span>
      </template>

      <a-table :data-source="gpuEvents" row-key="event_id" style="width: 100%">
        <a-table-column title="事件ID" :width="40">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="100">
              <template #content>
                <div>{{ record.event_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">{{ record.event_id.substring(0, 10) }}...</a-tag>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="设备ID" :width="80">
          <template #default="{ record }">
            <span>{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备IP" :width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip }}</span>
          </template>
        </a-table-column>

        <a-table-column title="用户ID" :width="80">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :width="200">
              <template #content>
                <div>{{ record.user_id }}</div>
              </template>
              <div class="flex">
                <a-tag color="default">
                  {{
                    record.user_id.length > 20 ? record.user_id.substring(0, 20) : ''
                  }}...</a-tag
                >
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="GPU名称" :width="140">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.gpu_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="使用率" :width="80">
          <template #default="{ record }">
            <span class="!font-bold text-amber-600">{{ record.gpu_usage }}%</span>
          </template>
        </a-table-column>

        <a-table-column title="上报时间" :width="120">
          <template #default="{ record }">
            <span class="!font-semibold">{{ record.readable_timestamp }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleCopyEvent(index, record)">
              复制
            </a-button>
          </template>
        </a-table-column>
      </a-table>

      <div class="h-5" />

      <div class="flex justify-center">
        <a-pagination
          v-model:current="gpuCurrentPage"
          v-model:page-size="gpuPageSize"
          :page-size-options="[20, 40, 60, 80]"
          :total="totalGpuSize"
          show-size-changer
          @change="handleGpuPageChange"
        />
      </div>
      <div class="h-5" />
    </a-tab-pane>
  </a-tabs>
</template>

<style scoped>
.custom-tabs :deep(.ant-tabs-content-holder) {
  padding: 32px;
  color: #6b778c;
  font-size: 32px;
  font-weight: 600;
}
.custom-tabs .custom-tabs-label {
  display: inline-flex;
  align-items: center;
}
.custom-tabs .custom-tabs-label span {
  vertical-align: middle;
  margin-left: 4px;
}
</style>
