<script setup lang="ts">
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import type { ClientConn } from '@/entity/client_conn.ts'
import { formatDuration } from '@/util/time.ts'
import { copyText } from '@/util/clipboard.ts'
import { ElNotification } from 'element-plus'

const totalFileTransferSize = ref(0)
// file transfer histories
const fileTransfers = ref<ClientConn[]>([])

const handleCopyClientConnInfo = (index: number, conn: ClientConn) => {
  console.log(index, conn)
  copyText(JSON.stringify(conn))
  ElNotification({
    title: '复制成功',
    type: 'success',
  })
}

// request file transfer history
async function queryOnlineClients(page: number, pageSize: number) {
  const resp = await axiosHttp.get('/api/v1/client/control/query/alive/conns', {
    params: {
      page: page,
      page_size: pageSize,
      appkey: localStorage.getItem('appkey'),
      sort_time: -1,
    },
  })
  if (resp.status !== 200) {
    console.error('requestVisits failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('requestVisits failed, data:', data)
    return
  }

  fileTransfers.value = data.data
  if (fileTransfers.value.length > 0 && fileTransfers.value[0] !== null) {
    totalFileTransferSize.value = fileTransfers.value[0]!.total
  }

  console.log('online clients: ', fileTransfers.value)
}

// onMounted
onMounted(async () => {
  await queryOnlineClients(1, 20)
})
</script>

<template>
  <div>
    <!-- file transfer list -->
    <el-table :data="fileTransfers" style="width: 100%">
      <el-table-column label="连接ID" :min-width="80">
        <template #default="scope">
          <el-popover effect="dark" trigger="hover" placement="top" width="200px">
            <template #default>
              <div>{{ scope.row.conn_id }}</div>
            </template>
            <template #reference>
              <div class="flex">
                <el-tag type="info">{{ scope.row.conn_id.substring(0, 20) }}...</el-tag>
                <div class="w-1" />
              </div>
            </template>
          </el-popover>
        </template>
      </el-table-column>

      <el-table-column label="发起设备" :min-width="80">
        <template #default="scope">
          <span>{{ scope.row.device_id }}</span>
        </template>
      </el-table-column>

      <el-table-column label="目标设备" :min-width="80">
        <template #default="scope">
          <span>{{ scope.row.remote_device_id + scope.row.remote_device_ip }}</span>
        </template>
      </el-table-column>

      <el-table-column label="开始时间" :min-width="100">
        <template #default="scope">
          <span>{{ scope.row.readable_hello_ts }}</span>
        </template>
      </el-table-column>

      <el-table-column label="结束时间" :min-width="100">
        <template #default="scope">
          <span class="">{{ scope.row.readable_update_ts }}</span>
        </template>
      </el-table-column>

      <el-table-column label="连接时间" :min-width="60">
        <template #default="scope">
          <span>{{
            formatDuration(scope.row.last_update_timestamp - scope.row.hello_timestamp)
          }}</span>
        </template>
      </el-table-column>

      <el-table-column label="操作">
        <template #default="scope">
          <el-button size="small" @click="handleCopyClientConnInfo(scope.$index, scope.row)">
            复制
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
