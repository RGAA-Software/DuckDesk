<script setup lang="ts">
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import type { ClientConn } from '@/entity/client_conn.ts'
import { formatDuration } from '@/util/time.ts'
import { copyText } from '@/util/clipboard.ts'
import { notification } from 'ant-design-vue'

const totalFileTransferSize = ref(0)
// file transfer histories
const fileTransfers = ref<ClientConn[]>([])

const handleCopyClientConnInfo = (index: number, conn: ClientConn) => {
  console.log(index, conn)
  copyText(JSON.stringify(conn))
  notification.success({
    message: '复制成功',
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
    <a-table :data-source="fileTransfers" row-key="conn_id" style="width: 100%">
      <template #emptyText><span>暂无数据</span></template>
      <a-table-column title="连接ID" min-width="80">
        <template #default="{ record }">
          <a-popover placement="top" trigger="hover" :overlay-style="{ width: '200px' }">
            <template #content>
              <div>{{ record.conn_id }}</div>
            </template>
            <div class="flex">
              <a-tag color="default">{{ record.conn_id.substring(0, 20) }}...</a-tag>
              <div class="w-1" />
            </div>
          </a-popover>
        </template>
      </a-table-column>

      <a-table-column title="发起设备" min-width="80">
        <template #default="{ record }">
          <span>{{ record.device_id }}</span>
        </template>
      </a-table-column>

      <a-table-column title="目标设备" min-width="80">
        <template #default="{ record }">
          <span>{{ record.remote_device_id + record.remote_device_ip }}</span>
        </template>
      </a-table-column>

      <a-table-column title="开始时间" min-width="100">
        <template #default="{ record }">
          <span>{{ record.readable_hello_ts }}</span>
        </template>
      </a-table-column>

      <a-table-column title="结束时间" min-width="100">
        <template #default="{ record }">
          <span class="">{{ record.readable_update_ts }}</span>
        </template>
      </a-table-column>

      <a-table-column title="连接时间" min-width="60">
        <template #default="{ record }">
          <span>{{
            formatDuration(record.last_update_timestamp - record.hello_timestamp)
          }}</span>
        </template>
      </a-table-column>

      <a-table-column title="操作">
        <template #default="{ index, record }">
          <a-button size="small" @click="handleCopyClientConnInfo(index, record)">
            复制
          </a-button>
        </template>
      </a-table-column>
    </a-table>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
