<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { formatTimestamp } from '@/util/time.ts'
import axiosHttp, { BASE_URL } from '@/http.ts'
import type { SpvrUser } from '@/entity/spvr_user.ts'
import { copyText } from '@/util/clipboard.ts'
import { type ComponentSize, ElNotification } from 'element-plus'
import { Picture } from '@element-plus/icons-vue'

const appkey = ref<string>('')
const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<ComponentSize>('default')
const background = ref(false)
const disabled = ref(false)
const searchUserId = ref('')
const searchUserName = ref('')
const handleSearch = async () => {
  await queryUsers(currentPage.value, pageSize.value, searchUserId.value, searchUserName.value)
}

const handleSizeChange = async (val: number) => {
  pageSize.value = val
  console.log('current page: ', currentPage.value, ' page size: ', pageSize.value)
  await queryUsers(currentPage.value, pageSize.value, '', '')
}

const handleCurrentChange = async (val: number) => {
  currentPage.value = val
  await queryUsers(currentPage.value, pageSize.value, '', '')
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
  ElNotification({
    message: '拷贝成功',
    type: 'success',
  })
}
</script>

<template>
  <div>
    <el-card class="w-full" shadow="hover">
      <div class="flex">
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">用户ID</span>
          <div class="h-2" />
          <el-input class="" v-model="searchUserId" placeholder="请输入"></el-input>
        </div>

        <div class="w-5" />
        <div class="w-40 flex flex-col items-start">
          <span class="!text-sm">用户名</span>
          <div class="h-2" />
          <el-input class="" v-model="searchUserName" placeholder="请输入"></el-input>
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

    <el-card class="w-full" shadow="never">
      <template #header>
        <div class="">
          <span class="text-lg font-bold text-slate-800">设备列表</span>
        </div>
      </template>

      <el-table :data="users" style="width: 100%">
        <el-table-column label="头像" :min-width="30">
          <template #default="scope">
            <el-image
              class="w-10 h-10 rounded-full overflow-hidden"
              :src="BASE_URL + scope.row.avatar_path + '?appkey=' + appkey"
              :fit="'cover'"
            >
              <template #error>
                <div class="image-error">
                  <el-icon><Picture /></el-icon>
                </div>
              </template>
            </el-image>
          </template>
        </el-table-column>

        <el-table-column label="UID" :min-width="80">
          <template #default="scope">
            <el-popover effect="dark" trigger="hover" placement="top" width="120px">
              <template #default>
                <div>{{ scope.row.uid }}</div>
              </template>
              <template #reference>
                <div class="flex">
                  <el-tag>{{ scope.row.uid.substring(0, 15) }}...</el-tag>
                </div>
              </template>
            </el-popover>
          </template>
        </el-table-column>

        <el-table-column label="用户名" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.username }}</span>
          </template>
        </el-table-column>

        <el-table-column label="是否启用" :min-width="60">
          <template #default="scope">
            <el-tag :type="!scope.row.deleted ? 'success' : 'danger'" effect="light">
              {{ !scope.row.deleted ? '已启用' : '已禁用' }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="创建时间" :min-width="100">
          <template #default="scope">
            <span class="!text-small">{{ formatTimestamp(scope.row.created_timestamp) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="更新时间" :min-width="100">
          <template #default="scope">
            <span class="!text-small">{{ formatTimestamp(scope.row.update_timestamp) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作" :min-width="200">
          <template #default="scope">
            <el-button size="small" @click="handleCopy(scope.$index, scope.row)"> 复制 </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <div class="h-3" />

    <div class="flex justify-center">
      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        :background="background"
        layout="total, sizes, prev, pager, next, jumper"
        :total="users.length"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
      />
    </div>
  </div>
</template>

<style scoped>
.image-error {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
  height: 100%;
  background: #f5f7fa;
  color: #c0c4cc;
}
</style>
