<script setup lang="ts">
import { IpAudit, IpLockOne, IpSettingTwo, IpConnectionPoint } from 'vue-icons-plus/ip'
import { onMounted, ref } from 'vue'
import axiosHttp from '@/http.ts'
import type { Authorization } from '@/entity/authorization.ts'
import { formatTimestamp, formatTimeToDays } from '@/util/time.ts'
import { ElNotification, type UploadFile } from 'element-plus'
import { queryAuthorization, updateAuthorization } from '@/model/auth_api.ts'
import CryptoJS from 'crypto-js'

const md5 = (input: string): string => {
  return CryptoJS.MD5(input).toString()
}

import { useRouter } from 'vue-router'
import {copyText} from "@/util/clipboard.ts";
const router = useRouter()

const authorization = ref<Authorization>()
const newPassword = ref<string>('')
const oldPassword = ref<string>('')
const repeatOldPassword = ref<string>('')
const accessInfo = ref<string>('')

onMounted(async () => {
  authorization.value = await queryAuthorization()
  await queryAccessInfo()
})

async function handleChangePassword() {
  if (
    oldPassword.value?.length === 0 ||
    repeatOldPassword.value?.length === 0 ||
    newPassword.value?.length === 0
  ) {
    ElNotification({
      message: '请输入合法密码',
      type: 'error',
    })
    return
  }
  if (oldPassword.value !== repeatOldPassword.value) {
    ElNotification({
      message: '两次输入的旧密码不一致',
      type: 'error',
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
    ElNotification({
      message: '密码修改失败:' + data.code,
      type: 'error',
    })
  } else {
    console.log('change password success')
    authorization.value = data.data
    ElNotification({
      message: '密码修改成功',
      type: 'success',
    })
  }
}

const handleSelectAuthFile = async (file: UploadFile) => {
  console.log(file)
  if (!file.raw) {
    console.error('file not found', file)
    return
  }

  const reader = new FileReader()
  reader.readAsText(file.raw, 'utf-8')

  reader.onload = async () => {
    const content = reader.result as string
    console.log('文件内容:', content)
    authorization.value = await updateAuthorization(content)
  }

  reader.onerror = () => {
    ElNotification({
      message: '读取文件失败',
      type: 'error',
    })
  }
}

//
async function handleLogout() {
  localStorage.setItem('token', '')
  await router.replace('/')
}

async function handleJumpOffSite() {
  window.open('https://godesk.uk', '_blank', 'noopener,noreferrer')
}

// access info
async function queryAccessInfo() {
  const resp = await axiosHttp.get(
    '/api/v1/spvr/control/gen/access/info?appkey=' + localStorage.getItem('appkey'),
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
  ElNotification({
    message: '复制授权信息成功',
    type: 'success',
  })
}
</script>

<template>
  <div class="w-full h-full">
    <el-tabs class="custom-tabs pl-3">
      <!--              -->
      <!--              -->
      <!--              -->
      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><IpConnectionPoint /></el-icon>
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

          <el-button type="primary" class="w-20" @click="handleCopyAccessInfo">复制</el-button>
        </div>
      </el-tab-pane>
      <!--              -->
      <!--              -->
      <!--              -->
      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><IpAudit /></el-icon>
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
                authorization?.used_time_ms ? formatTimeToDays(authorization?.used_time_ms) : 0
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
              <el-upload :auto-upload="false" :on-change="handleSelectAuthFile" :limit="1">
                <el-button type="primary">选择授权文件</el-button>
              </el-upload>
            </div>
          </div>
          <div class="h-2" />

          <div class="flex items-center">
            <div class="w-35 text-slate-500">获取授权</div>
            <div class="flex items-center justify-center">
              <span>授权过期? 点击获取免费新授权</span>
              <span class="w-2"></span>
              <el-button type="warning" @click="handleJumpOffSite">获取授权</el-button>
            </div>
          </div>
          <div class="h-2" />
        </div>
      </el-tab-pane>

      <!--              -->
      <!--              -->
      <!--              -->
      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><IpLockOne /></el-icon>
            <span>修改密码</span>
          </span>
        </template>
        <div>
          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">新密码</div>
            <div class="w-50">
              <el-input
                v-model="newPassword"
                placeholder="请输入"
                type="password"
                show-password
              ></el-input>
            </div>
          </div>

          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">旧密码</div>
            <div class="w-50">
              <el-input
                v-model="oldPassword"
                placeholder="请输入"
                type="password"
                show-password
              ></el-input>
            </div>
          </div>

          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">重复旧密码</div>
            <div class="w-50">
              <el-input
                v-model="repeatOldPassword"
                placeholder="请输入"
                type="password"
                show-password
              ></el-input>
            </div>
          </div>

          <div class="h-5" />
          <div class="flex items-center h-8">
            <el-button class="w-25" type="primary" @click="handleChangePassword">确定</el-button>
            <div class="w-50"></div>
          </div>
        </div>
      </el-tab-pane>

      <el-tab-pane>
        <template #label>
          <span class="custom-tabs-label">
            <el-icon><IpSettingTwo /></el-icon>
            <span>通用设置</span>
          </span>
        </template>
        <div>
          <div class="h-2" />
          <div class="flex items-center h-8">
            <div class="w-25 text-slate-500">退出登录</div>
            <div class="w-50">
              <el-button type="danger" class="w-20" @click="handleLogout">退出</el-button>
            </div>
          </div>
        </div>
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
