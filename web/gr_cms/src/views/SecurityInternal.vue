<script setup lang="ts">
import { Connection, Files } from '@element-plus/icons-vue'
import { onMounted, ref } from 'vue'
import { formatDuration, formatTimestamp } from '@/util/time.ts'
import type { Visit } from '@/entity/visit.ts'
import axiosHttp from '@/http.ts'
import type { FileTransfer } from '@/entity/file_transfer.ts'
import { copyText } from '@/util/clipboard.ts'
import { ElNotification } from 'element-plus'

const visitDeviceId = ref('')
const targetDeviceId = ref('')

// -------------------------------------Visit-----------------------------------------
const visitPageSize = ref(20)
const visitCurrentPage = ref(1)
const totalVisitSize = ref(0)

// visit histories
const visits = ref<Visit[]>([])

const handleSizeChange = (val: number) => {
  visitPageSize.value = val
  console.log('current page: ', visitCurrentPage.value, ' page size: ', visitPageSize.value)
  queryVisits(visitCurrentPage.value, visitPageSize.value, '', '')
}

const handleCurrentChange = (val: number) => {
  visitCurrentPage.value = val
  queryVisits(visitCurrentPage.value, visitPageSize.value, '', '')
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
  }

  //console.log('device list, total: ', totalVisitSize.value, ', devices: ', visits.value)
}

// -------------------------------------File Transfer-----------------------------------------
const fileTransferPageSize = ref(20)
const fileTransferCurrentPage = ref(1)
const totalFileTransferSize = ref(0)
// file transfer histories
const fileTransfers = ref<FileTransfer[]>([])

const handleFileTransferSizeChange = (val: number) => {
  fileTransferPageSize.value = val
  console.log(
    'current page: ',
    fileTransferCurrentPage.value,
    ' page size: ',
    fileTransferPageSize.value,
  )
  queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', '')
}

const handleFileTransferCurrentChange = (val: number) => {
  fileTransferCurrentPage.value = val
  queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', '')
}

const handleCopyVisitInfo = (index: number, visit: Visit) => {
  console.log(index, visit)
  copyText(JSON.stringify(visit))
  ElNotification({
    title: '复制成功',
    type: 'success',
  })
}

const handleCopyFileTransferInfo = (index: number, ft: FileTransfer) => {
  console.log(index, ft)
  copyText(JSON.stringify(ft))
  ElNotification({
    title: '复制成功',
    type: 'success',
  })
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
    console.error('queryVisits failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryVisits failed, data:', data)
    return
  }

  fileTransfers.value = data.data
  if (fileTransfers.value.length > 0 && fileTransfers.value[0] !== null) {
    totalFileTransferSize.value = fileTransfers.value[0]!.total
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
</script>

<template>
  <div>
    <el-card class="w-full" shadow="hover">
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">发起设备ID</span>
          <div class="h-2" />
          <el-input class="" v-model="visitDeviceId" placeholder="请输入"></el-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">目标设备ID</span>
          <div class="h-2" />
          <el-input class="" v-model="targetDeviceId" placeholder="请输入"></el-input>
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
      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><connection /></el-icon>
            <span>访问记录</span>
          </span>
        </template>

        <!-- visit list -->
        <el-table :data="visits" style="width: 100%">
          <el-table-column label="连接类型" :min-width="80">
            <template #default="scope">
              <el-tag
                :type="scope.row.conn_type === 'Direct' ? 'success' : 'primary'"
                effect="light"
              >
                {{ scope.row.conn_type === 'Direct' ? '直连' : '中转' }}
              </el-tag>
            </template>
          </el-table-column>

          <el-table-column label="开始时间" :min-width="160">
            <template #default="scope">
              <span class="">{{ formatTimestamp(scope.row.begin) }}</span>
            </template>
          </el-table-column>

          <el-table-column label="结束时间" :min-width="160">
            <template #default="scope">
              <span class="">{{ scope.row.end > 0 ? formatTimestamp(scope.row.end) : 0 }}</span>
            </template>
          </el-table-column>

          <el-table-column label="连接时长" :min-width="120">
            <template #default="scope">
              <span>{{ formatDuration(scope.row.duration) }}</span>
            </template>
          </el-table-column>

          <el-table-column label="发起设备" :min-width="120">
            <template #default="scope">
              <span class="!font-semibold">{{ scope.row.visitor_device }}</span>
            </template>
          </el-table-column>

          <el-table-column label="目标设备" :min-width="120">
            <template #default="scope">
              <span class="!font-semibold">{{ scope.row.target_device }}</span>
            </template>
          </el-table-column>

          <el-table-column label="操作">
            <template #default="scope">
              <el-button size="small" @click="handleCopyVisitInfo(scope.$index, scope.row)">
                复制
              </el-button>

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

        <div class="h-5" />
        <div class="flex justify-center">
          <el-pagination
            v-model:current-page="visitCurrentPage"
            v-model:page-size="visitPageSize"
            :page-sizes="[20, 40, 60, 80]"
            layout="total, sizes, prev, pager, next, jumper"
            :total="totalVisitSize"
            @size-change="handleSizeChange"
            @current-change="handleCurrentChange"
          />
        </div>
        <div class="h-5" />
      </el-tab-pane>

      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><Files /></el-icon>
            <span>文件传输</span>
          </span>
        </template>

        <!-- file transfer list -->
        <el-table :data="fileTransfers" style="width: 100%">
          <el-table-column label="文件ID" :min-width="80">
            <template #default="scope">
              <el-popover effect="dark" trigger="hover" placement="top" width="200px">
                <template #default>
                  <div>{{ scope.row.the_file_id }}</div>
                </template>
                <template #reference>
                  <div class="flex">
                    <el-tag type="info">{{ scope.row.the_file_id.substring(0, 10) }}...</el-tag>
                    <div class="w-1" />
                  </div>
                </template>
              </el-popover>
            </template>
          </el-table-column>

          <el-table-column label="发起设备" :min-width="80">
            <template #default="scope">
              <span>{{ scope.row.visitor_device }}</span>
            </template>
          </el-table-column>

          <el-table-column label="目标设备" :min-width="80">
            <template #default="scope">
              <span>{{ scope.row.target_device }}</span>
            </template>
          </el-table-column>

          <el-table-column label="开始时间" :min-width="100">
            <template #default="scope">
              <span>{{ formatTimestamp(scope.row.begin) }}</span>
            </template>
          </el-table-column>

          <el-table-column label="结束时间" :min-width="100">
            <template #default="scope">
              <span class="">{{ formatTimestamp(scope.row.end) }}</span>
            </template>
          </el-table-column>

          <el-table-column label="传输方向" :min-width="60">
            <template #default="scope">
              <el-tag :type="scope.row.direction === 'In' ? 'success' : 'primary'" effect="light">
                {{ scope.row.direction === 'In' ? '传入' : '传出' }}
              </el-tag>
            </template>
          </el-table-column>

          <el-table-column label="文件路径" :min-width="120">
            <template #default="scope">
              <el-popover effect="dark" trigger="hover" placement="top" width="300px">
                <template #default>
                  <div>{{ scope.row.file_detail }}</div>
                </template>
                <template #reference>
                  <div class="flex">
                    <el-tag type="info">{{ scope.row.file_detail.substring(0, 30) }}...</el-tag>
                    <div class="w-1" />
                  </div>
                </template>
              </el-popover>
            </template>
          </el-table-column>

          <el-table-column label="操作">
            <template #default="scope">
              <el-button size="small" @click="handleCopyFileTransferInfo(scope.$index, scope.row)">
                复制
              </el-button>
            </template>
          </el-table-column>
        </el-table>

        <div class="h-5" />

        <div class="flex justify-center">
          <el-pagination
            v-model:current-page="fileTransferCurrentPage"
            v-model:page-size="fileTransferPageSize"
            :page-sizes="[20, 40, 60, 80]"
            layout="total, sizes, prev, pager, next, jumper"
            :total="totalFileTransferSize"
            @size-change="handleFileTransferSizeChange"
            @current-change="handleFileTransferCurrentChange"
          />
        </div>
        <div class="h-5" />
      </el-tab-pane>
    </el-tabs>
  </div>
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
