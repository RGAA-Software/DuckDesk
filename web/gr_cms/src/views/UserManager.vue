<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { notification } from 'ant-design-vue'
import { formatTimestamp } from '@/util/time.ts'
import axiosHttp, { BASE_URL } from '@/http.ts'
import type { SpvrUser } from '@/entity/spvr_user.ts'
import { copyText } from '@/util/clipboard.ts'

const appkey = ref<string>('')
const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<'small' | 'default'>('default')
const disabled = ref(false)
const searchUserId = ref('')
const searchUserName = ref('')
const handleSearch = async () => {
  await queryUsers(currentPage.value, pageSize.value, searchUserId.value, searchUserName.value)
}

const handlePageChange = async (page: number, size: number) => {
  currentPage.value = page
  pageSize.value = size
  await queryUsers(page, size, '', '')
}

onMounted(async () => {
  const ak = localStorage.getItem('appkey')
  if (ak !== null) {
    appkey.value = ak
  }
  await queryUsers(currentPage.value, pageSize.value, '', '')
})

const users = ref<SpvrUser[]>([])

async function queryUsers(page: number, pageSize: number, uid: string, username: string) {
  const resp = await axiosHttp.get('/api/v1/user/control/query/users', {
    params: {
      page: page,
      page_size: pageSize,
      uid: uid,
      username: username,
      appkey: localStorage.getItem('appkey'),
    },
  })
  if (resp.status !== 200) {
    console.error('query users failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('query users failed, data:', data)
    return null
  }
  console.log('users: ', data.data)
  users.value = data.data
}

async function handleCopy(_index: number, user: SpvrUser) {
  await copyText(JSON.stringify(user))
  notification.success({
    message: '拷贝成功',
  })
}
</script>

<template>
  <div>
    <a-card class="w-full" hoverable>
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">用户ID</span>
          <div class="h-2" />
          <a-input class="" v-model:value="searchUserId" placeholder="请输入"></a-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">用户名</span>
          <div class="h-2" />
          <a-input class="" v-model:value="searchUserName" placeholder="请输入"></a-input>
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

    <a-card class="w-full" :bordered="false">
      <template #title>
        <div class="">
          <span class="text-lg font-bold text-slate-800">设备列表</span>
        </div>
      </template>

      <a-table :data-source="users" style="width: 100%" row-key="uid">
        <a-table-column title="头像" :min-width="30">
          <template #default="{ record }">
            <a-image
              class="w-10 h-10 rounded-full overflow-hidden"
              :src="BASE_URL + record.avatar_path + '?appkey=' + appkey"
              :preview="false"
            />
          </template>
        </a-table-column>

        <a-table-column title="UID" :min-width="80">
          <template #default="{ record }">
            <a-popover trigger="hover" placement="top" :overlay-inner-style="{ width: '120px' }">
              <template #content>
                <div>{{ record.uid }}</div>
              </template>
              <div class="flex">
                <a-tag>{{ record.uid.substring(0, 15) }}...</a-tag>
              </div>
            </a-popover>
          </template>
        </a-table-column>

        <a-table-column title="用户名" :min-width="80">
          <template #default="{ record }">
            <span class="">{{ record.username }}</span>
          </template>
        </a-table-column>

        <a-table-column title="是否启用" :min-width="60">
          <template #default="{ record }">
            <a-tag :color="!record.deleted ? 'success' : 'error'">
              {{ !record.deleted ? '已启用' : '已禁用' }}
            </a-tag>
          </template>
        </a-table-column>

        <a-table-column title="创建时间" :min-width="100">
          <template #default="{ record }">
            <span class="!text-small">{{ formatTimestamp(record.created_timestamp) }}</span>
          </template>
        </a-table-column>

        <a-table-column title="更新时间" :min-width="100">
          <template #default="{ record }">
            <span class="!text-small">{{ formatTimestamp(record.update_timestamp) }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作" :min-width="200">
          <template #default="{ record, index }">
            <a-button size="small" @click="handleCopy(index, record)"> 复制 </a-button>
          </template>
        </a-table-column>
      </a-table>
    </a-card>

    <div class="h-3" />

    <div class="flex justify-center">
      <a-pagination
        v-model:current="currentPage"
        v-model:page-size="pageSize"
        :page-size-options="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        show-size-changer
        :total="users.length"
        @change="handlePageChange"
      />
    </div>
  </div>
</template>

<style scoped></style>
