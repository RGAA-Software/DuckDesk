<script setup lang="ts">
import { ApiOutlined, AuditOutlined, LockOutlined, SettingOutlined } from '@ant-design/icons-vue'
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import { formatDurationHMS, formatTimestamp } from '@/util/time.ts'
import { notification } from 'ant-design-vue'
import { pullAuthorization } from '@/model/auth_api.ts'
import { refreshSharedAuthorization, sharedAuthorization as authorization } from '@/model/auth_state.ts'
import CryptoJS from 'crypto-js'

const md5 = (input: string): string => {
  return CryptoJS.MD5(input).toString()
}

import { useRouter } from 'vue-router'
import {copyText} from "@/util/clipboard.ts";
const router = useRouter()

const newPassword = ref<string>('')
const oldPassword = ref<string>('')
const repeatOldPassword = ref<string>('')
const accessInfo = ref<string>('')

onMounted(async () => {
  await refreshSharedAuthorization()
  await queryAccessInfo()
})

async function handleChangePassword() {
  if (
    oldPassword.value?.length === 0 ||
    repeatOldPassword.value?.length === 0 ||
    newPassword.value?.length === 0
  ) {
    notification.error({
      message: '请输入合法密码',
    })
    return
  }
  if (oldPassword.value !== repeatOldPassword.value) {
    notification.error({
      message: '两次输入的旧密码不一致',
    })
    return
  }

  const resp = await axiosHttp.post(
    '/api/v1/auth/control/update/password?appkey=' + localStorage.getItem('appkey'),
    {
      password: newPassword.value,
      old_password: md5(oldPassword.value),
    },
  )
  if (resp.status !== 200) {
    console.error('change password failed', resp)
    return false
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryDevices failed, data:', data)
    notification.error({
      message: '密码修改失败:' + data.code,
    })
  } else {
    console.log('change password success')
    authorization.value = data.data
    notification.success({
      message: '密码修改成功',
    })
  }
}

const handleRefreshAuth = async () => {
  // pull 返回的是安全状态（不含凭据），完整授权信息需带 appkey 重新查询；
  // 走共享状态，右上角（HomeView）授权状态会同步更新
  await pullAuthorization()
  await refreshSharedAuthorization()
}

//
async function handleLogout() {
  sessionStorage.removeItem('admin_authenticated')
  await router.replace('/')
}

async function handleJumpOffSite() {
  window.open('https://pixels.yun', '_blank', 'noopener,noreferrer')
}

// access info
async function queryAccessInfo() {
  const resp = await axiosHttp.get(
    '/api/v1/cms/control/gen/access/info?appkey=' + localStorage.getItem('appkey'),
  )
  if (resp.status !== 200) {
    console.error('query access info failed', resp)
    return false
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('query access info failed, resp:', resp)
    return
  }
  accessInfo.value = data.data
}

// access info
async function handleCopyAccessInfo() {
  if (!accessInfo.value) {
    return;
  }
  await copyText(accessInfo.value)
  notification.success({
    message: '复制授权信息成功',
  })
}
</script>

<template>
  <div class="w-full h-full">
    <a-tabs class="custom-tabs pl-3">
      <!--              -->
      <!--              -->
      <!--              -->
      <a-tab-pane key="client-access">
        <template #tab>
          <span class="custom-tabs-label">
            <ApiOutlined />
            <span>客户端接入</span>
          </span>
        </template>
        <div class="flex items-center">
          <div class="w-35 text-slate-500">授权信息</div>
          <div class="w-[500px]">
            <div class="p-3 rounded-lg bg-gray-100 font-mono text-sm break-all whitespace-pre-wrap">
              {{ accessInfo }}
            </div>
          </div>

          <div class="w-5"></div>

          <a-button type="primary" class="w-20" @click="handleCopyAccessInfo">复制</a-button>
        </div>
      </a-tab-pane>
      <!--              -->
      <!--              -->
      <!--              -->
      <a-tab-pane key="auth-info">
        <template #tab>
          <span class="custom-tabs-label">
            <AuditOutlined />
            <span>授权信息</span>
          </span>
        </template>
        <div>
          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">ID</div>
            <div class="text-medium text-slate-600 font-semibold">{{ authorization?.auth_id }}</div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">App Key</div>
            <div class="text-medium text-slate-600 font-semibold">{{ authorization?.appkey }}</div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">App Secret</div>
            <div class="text-medium text-slate-600 font-semibold">
              {{ authorization?.app_secret || '-' }}
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">用户名</div>
            <div class="text-medium text-slate-600 font-semibold">
              {{ authorization?.username || '-' }}
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">创建时间</div>
            <div class="text-medium text-slate-600 font-semibold">
              {{ authorization?.created_timestamp_ms ? formatTimestamp(authorization.created_timestamp_ms) : '-' }}
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">授权天数</div>
            <div v-if="authorization" class="text-medium text-amber-600 font-semibold">
              {{ authorization?.days }}天, 已使用{{
                authorization?.used_time_ms ? formatDurationHMS(authorization?.used_time_ms) : 0
              }}
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">最大连接数</div>
            <div class="text-medium text-amber-600 font-semibold">
              {{ authorization?.max_streams }}
            </div>
          </div>

          <div class="h-2" />

          <div class="flex items-center h-8">
            <div class="w-35 text-slate-500">机器码</div>
            <div class="text-medium text-slate-600 font-semibold">
              {{ authorization?.machine_code }}
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center h-18">
            <div class="w-35 text-slate-500">更新授权</div>
            <div>
              <a-button type="primary" @click="handleRefreshAuth">刷新授权</a-button>
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center">
            <div class="w-35 text-slate-500">获取授权</div>
            <div class="flex items-center justify-center">
              <span>授权过期? 点击获取免费新授权</span>
              <span class="w-2"></span>
              <a-button @click="handleJumpOffSite">获取授权</a-button>
            </div>
          </div>
          <div class="h-2" />
        </div>
      </a-tab-pane>

      <!--              -->
      <!--              -->
      <!--              -->
      <a-tab-pane key="change-password">
        <template #tab>
          <span class="custom-tabs-label">
            <LockOutlined />
            <span>修改密码</span>
          </span>
        </template>
        <div>
          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">新密码</div>
            <div class="w-50">
              <a-input-password v-model:value="newPassword" placeholder="请输入" />
            </div>
          </div>

          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">旧密码</div>
            <div class="w-50">
              <a-input-password v-model:value="oldPassword" placeholder="请输入" />
            </div>
          </div>

          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">重复旧密码</div>
            <div class="w-50">
              <a-input-password v-model:value="repeatOldPassword" placeholder="请输入" />
            </div>
          </div>

          <div class="h-5" />
          <div class="flex items-center h-8">
            <a-button class="w-25" type="primary" @click="handleChangePassword">确定</a-button>
            <div class="w-50"></div>
          </div>
        </div>
      </a-tab-pane>

      <a-tab-pane key="general-settings">
        <template #tab>
          <span class="custom-tabs-label">
            <SettingOutlined />
            <span>通用设置</span>
          </span>
        </template>
        <div>
          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">退出登录</div>
            <div class="w-50">
              <a-button type="danger" class="w-20" @click="handleLogout">退出</a-button>
            </div>
          </div>
        </div>
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
  gap: 4px;
}
</style>
