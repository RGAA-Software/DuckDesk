<script setup lang="ts">
import { LinkOutlined, FolderOutlined } from '@ant-design/icons-vue'
import { onMounted, ref } from 'vue'
import { formatDuration, formatTimestamp } from '@/util/time.ts'
import { connTypeTagType, formatConnTypeLabel } from '@/util/conn_type.ts'
import type { Visit } from '@/entity/visit.ts'
import axiosHttp from '@/http.ts'
import type { FileTransfer } from '@/entity/file_transfer.ts'
import { copyText } from '@/util/clipboard.ts'
import { notification } from 'ant-design-vue'

const visitDeviceId = ref('')
const targetDeviceId = ref('')

// -------------------------------------Visit-----------------------------------------
const visitPageSize = ref(20)
const visitCurrentPage = ref(1)
const totalVisitSize = ref(0)

// visit histories
const visits = ref<Visit[]>([])

const handleVisitPageChange = (page: number, pageSize: number) => {
  visitCurrentPage.value = page
  visitPageSize.value = pageSize
  queryVisits(page, pageSize, '', '')
}

// request device
async function queryVisits(
  page: number,
  pageSize: number,
  visitDevice: string,
  targetDevice: string,
) {
  const resp = await axiosHttp.get('/api/v1/record/query_visit_info', {
    params: {
      page: page,
      page_size: pageSize,
      appkey: localStorage.getItem('appkey'),
      sort_time: -1,
      visit_device_id: visitDevice ? visitDevice.trim() : '',
      target_device_id: targetDevice ? targetDevice.trim() : '',
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

  visits.value = data.data
  if (visits.value.length > 0 && visits.value[0] !== null) {
    totalVisitSize.value = visits.value[0]!.total
  } else {
    totalVisitSize.value = 0
  }

  //console.log('device list, total: ', totalVisitSize.value, ', devices: ', visits.value)
}

// -------------------------------------File Transfer-----------------------------------------
const fileTransferPageSize = ref(20)
const fileTransferCurrentPage = ref(1)
const totalFileTransferSize = ref(0)
// file transfer histories
const fileTransfers = ref<FileTransfer[]>([])

const handleFileTransferPageChange = (page: number, pageSize: number) => {
  fileTransferCurrentPage.value = page
  fileTransferPageSize.value = pageSize
  queryFileTransfers(page, pageSize, '', '')
}

const handleCopyVisitInfo = (index: number, visit: Visit) => {
  console.log(index, visit)
  copyText(JSON.stringify(visit))
  notification.success({
    message: '复制成功',
  })
}

const handleCopyFileTransferInfo = (index: number, ft: FileTransfer) => {
  console.log(index, ft)
  copyText(JSON.stringify(ft))
  notification.success({
    message: '复制成功',
  })
}

// map conn_type util tag types (success/primary/warning/info/danger) to antd tag colors
const connTagColor = (type: string): string => {
  switch (type) {
    case 'success':
      return 'success'
    case 'primary':
      return 'processing'
    case 'warning':
      return 'warning'
    case 'danger':
      return 'error'
    default:
      return 'default'
  }
}

// request file transfer history
async function queryFileTransfers(
  page: number,
  pageSize: number,
  visitDevice: string,
  targetDevice: string,
) {
  const resp = await axiosHttp.get('/api/v1/record/query_file_transfer_info', {
    params: {
      page: page,
      page_size: pageSize,
      appkey: localStorage.getItem('appkey'),
      sort_time: -1,
      visit_device_id: visitDevice ? visitDevice.trim() : '',
      target_device_id: targetDevice ? targetDevice.trim() : '',
    },
  })
  if (resp.status !== 200) {
    console.error('queryFileTransfers failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryFileTransfers failed, data:', data)
    return
  }

  fileTransfers.value = data.data
  if (fileTransfers.value.length > 0 && fileTransfers.value[0] !== null) {
    totalFileTransferSize.value = fileTransfers.value[0]!.total
  } else {
    totalFileTransferSize.value = 0
  }

  //console.log('device list, total: ', totalFileTransferSize.value, ', filetransfer: ', fileTransfers.value)
}

// onMounted
onMounted(async () => {
  await queryVisits(visitCurrentPage.value, visitPageSize.value, '', '')
  await queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', '')
})

// search
const handleSearch = async () => {
  visitCurrentPage.value = 1
  fileTransferCurrentPage.value = 1
  // 1. search records
  await queryVisits(
    visitCurrentPage.value,
    visitPageSize.value,
    visitDeviceId.value,
    targetDeviceId.value,
  )
  // 2. search file transfers
  await queryFileTransfers(
    fileTransferCurrentPage.value,
    fileTransferPageSize.value,
    visitDeviceId.value,
    targetDeviceId.value,
  )
}

const handleClear = async () => {
  visitDeviceId.value = ''
  targetDeviceId.value = ''
  visitCurrentPage.value = 1
  fileTransferCurrentPage.value = 1
  await queryVisits(visitCurrentPage.value, visitPageSize.value, '', '')
  await queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', '')
}

const handleRefresh = async () => {
  await queryVisits(visitCurrentPage.value, visitPageSize.value, visitDeviceId.value, targetDeviceId.value)
  await queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, visitDeviceId.value, targetDeviceId.value)
}
</script>

<template>
  <div>
    <a-card class="w-full" hoverable>
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">发起设备ID</span>
          <div class="h-2" />
          <a-input class="" v-model:value="visitDeviceId" placeholder="请输入"></a-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">目标设备ID</span>
          <div class="h-2" />
          <a-input class="" v-model:value="targetDeviceId" placeholder="请输入"></a-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!h-5"></span>
          <div class="h-2" />
          <a-button class="w-20" type="primary" @click="handleSearch">搜索</a-button>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!h-5"></span>
          <div class="h-2" />
          <a-button class="w-20" @click="handleClear">清空</a-button>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!h-5"></span>
          <div class="h-2" />
          <a-button class="w-20" @click="handleRefresh">刷新</a-button>
        </div>
      </div>
    </a-card>

    <div class="h-2" />

    <a-tabs class="custom-tabs">
      <a-tab-pane key="visit">
        <template #tab>
          <span class="custom-tabs-label">
            <LinkOutlined />
            <span>访问记录</span>
          </span>
        </template>

        <!-- visit list -->
        <a-table :data-source="visits" row-key="conn_id" style="width: 100%">
          <a-table-column title="连接类型" :width="80">
            <template #default="{ record }">
              <a-tag :color="connTagColor(connTypeTagType(record.conn_type))">
                {{ formatConnTypeLabel(record.conn_type) }}
              </a-tag>
            </template>
          </a-table-column>

          <a-table-column title="开始时间" :width="160">
            <template #default="{ record }">
              <span class="">{{ formatTimestamp(record.begin) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="结束时间" :width="160">
            <template #default="{ record }">
              <span class="">{{ record.end > 0 ? formatTimestamp(record.end) : '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="连接时长" :width="120">
            <template #default="{ record }">
              <span>{{ formatDuration(record.duration) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="发起设备" :width="120">
            <template #default="{ record }">
              <span class="!font-semibold">{{ record.visitor_device }}</span>
            </template>
          </a-table-column>

          <a-table-column title="目标设备" :width="120">
            <template #default="{ record }">
              <span class="!font-semibold">{{ record.target_device }}</span>
            </template>
          </a-table-column>

          <a-table-column title="操作">
            <template #default="{ record, index }">
              <a-button size="small" @click="handleCopyVisitInfo(index, record)">
                复制
              </a-button>

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

        <div class="h-5" />
        <div class="flex justify-center">
          <a-pagination
            v-model:current="visitCurrentPage"
            v-model:page-size="visitPageSize"
            :page-size-options="[20, 40, 60, 80]"
            :total="totalVisitSize"
            show-size-changer
            @change="handleVisitPageChange"
          />
        </div>
        <div class="h-5" />
      </a-tab-pane>

      <a-tab-pane key="file-transfer">
        <template #tab>
          <span class="custom-tabs-label">
            <FolderOutlined />
            <span>文件传输</span>
          </span>
        </template>

        <!-- file transfer list -->
        <a-table :data-source="fileTransfers" row-key="the_file_id" style="width: 100%">
          <a-table-column title="文件ID" :width="80">
            <template #default="{ record }">
              <a-popover trigger="hover" placement="top" :width="200">
                <template #content>
                  <div>{{ record.the_file_id }}</div>
                </template>
                <div class="flex">
                  <a-tag color="default">{{ record.the_file_id.substring(0, 10) }}...</a-tag>
                  <div class="w-1" />
                </div>
              </a-popover>
            </template>
          </a-table-column>

          <a-table-column title="发起设备" :width="80">
            <template #default="{ record }">
              <span>{{ record.visitor_device }}</span>
            </template>
          </a-table-column>

          <a-table-column title="目标设备" :width="80">
            <template #default="{ record }">
              <span>{{ record.target_device }}</span>
            </template>
          </a-table-column>

          <a-table-column title="开始时间" :width="100">
            <template #default="{ record }">
              <span>{{ formatTimestamp(record.begin) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="结束时间" :width="100">
            <template #default="{ record }">
              <span class="">{{ record.end > 0 ? formatTimestamp(record.end) : '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="传输结果" :width="80">
            <template #default="{ record }">
              <a-tag :color="record.success ? 'success' : 'error'">
                {{ record.success ? '成功' : '失败/未完成' }}
              </a-tag>
            </template>
          </a-table-column>

          <a-table-column title="传输耗时" :width="100">
            <template #default="{ record }">
              <span>
                {{
                  record.duration != null && record.duration > 0
                    ? formatDuration(record.duration)
                    : record.end > 0 && record.begin > 0
                      ? formatDuration(record.end - record.begin)
                      : '-'
                }}
              </span>
            </template>
          </a-table-column>

          <a-table-column title="传输方向" :width="60">
            <template #default="{ record }">
              <a-tag :color="record.direction === 'In' ? 'success' : 'processing'">
                {{ record.direction === 'In' ? '传入' : '传出' }}
              </a-tag>
            </template>
          </a-table-column>

          <a-table-column title="文件路径" :width="120">
            <template #default="{ record }">
              <a-popover trigger="hover" placement="top" :width="300">
                <template #content>
                  <div>{{ record.file_detail }}</div>
                </template>
                <div class="flex">
                  <a-tag color="default">{{ record.file_detail.substring(0, 30) }}...</a-tag>
                  <div class="w-1" />
                </div>
              </a-popover>
            </template>
          </a-table-column>

          <a-table-column title="操作">
            <template #default="{ record, index }">
              <a-button size="small" @click="handleCopyFileTransferInfo(index, record)">
                复制
              </a-button>
            </template>
          </a-table-column>
        </a-table>

        <div class="h-5" />

        <div class="flex justify-center">
          <a-pagination
            v-model:current="fileTransferCurrentPage"
            v-model:page-size="fileTransferPageSize"
            :page-size-options="[20, 40, 60, 80]"
            :total="totalFileTransferSize"
            show-size-changer
            @change="handleFileTransferPageChange"
          />
        </div>
        <div class="h-5" />
      </a-tab-pane>
    </a-tabs>
  </div>
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
