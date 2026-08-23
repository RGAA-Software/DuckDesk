<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import {
  getRtcIceConfig,
  getTurnStatus,
  saveRtcIceConfig,
  testRtcIceConfig,
  type AdditionalIceServerConfig,
  type RtcIceConfig,
  type TurnSidecarStatus,
} from '@/model/rtc_api.ts'

interface IceServerEditor extends AdditionalIceServerConfig {
  urls_text: string
}

const loading = ref(false)
const saving = ref(false)
const testing = ref(false)
const config = ref<RtcIceConfig | null>(null)
const editors = ref<IceServerEditor[]>([])
const status = ref<TurnSidecarStatus | null>(null)

const statusText = computed(() => {
  if (!status.value?.enabled) return '已停用'
  return status.value.running ? '运行中' : '未运行'
})

function errorText(error: unknown) {
  const value = error as { response?: { data?: { message?: string } }; message?: string }
  return value.response?.data?.message || value.message || '操作失败'
}

function makePayload(): RtcIceConfig {
  if (!config.value) throw new Error('配置尚未加载')
  return {
    ...config.value,
    managed_console_server: { ...config.value.managed_console_server },
    additional_servers: editors.value.map(({ urls_text, ...server }) => ({
      ...server,
      urls: urls_text
        .split(/\r?\n/)
        .map((url) => url.trim())
        .filter(Boolean),
    })),
  }
}

function setConfig(value: RtcIceConfig) {
  config.value = structuredClone(value)
  editors.value = value.additional_servers.map((server) => ({
    ...structuredClone(server),
    urls_text: server.urls.join('\n'),
  }))
}

async function refresh() {
  loading.value = true
  try {
    const [nextConfig, nextStatus] = await Promise.all([getRtcIceConfig(), getTurnStatus()])
    setConfig(nextConfig)
    status.value = nextStatus
  } catch (error) {
    message.error(errorText(error))
  } finally {
    loading.value = false
  }
}

function addServer() {
  const id = `ice-${Date.now().toString(36)}`
  editors.value.push({
    id,
    name: '附加 ICE Server',
    enabled: true,
    urls: [],
    urls_text: 'stun:stun.example.com:3478',
    credential_mode: 'none',
    username: '',
    credential: '',
  })
}

function restoreDefaults() {
  if (!config.value) return
  const host = config.value.managed_console_server.public_host
  config.value.direct_probe_enabled = false
  config.value.managed_console_server = {
    enabled: true,
    listen_ip: '0.0.0.0',
    public_host: host,
    port: 20128,
    relay_min_port: 20200,
    relay_max_port: 20500,
    realm: 'pixels-console',
    enable_udp: true,
    enable_tcp: true,
    credential_ttl_seconds: 300,
  }
  editors.value = []
}

async function validate() {
  testing.value = true
  try {
    await testRtcIceConfig(makePayload())
    message.success('配置格式与端口参数校验通过')
  } catch (error) {
    message.error(errorText(error))
  } finally {
    testing.value = false
  }
}

async function save() {
  saving.value = true
  try {
    const saved = await saveRtcIceConfig(makePayload())
    setConfig(saved)
    status.value = await getTurnStatus()
    message.success(`配置已应用，revision ${saved.revision} 已推送到在线节点`)
  } catch (error) {
    message.error(errorText(error))
  } finally {
    saving.value = false
  }
}

onMounted(refresh)
</script>

<template>
  <div class="rtc-page">
    <a-spin :spinning="loading">
      <a-alert
        class="mb-4"
        type="info"
        show-icon
        message="net_rtc_local 保持直连专用；标准 net_rtc 会使用此处的多个 STUN/TURN Server，由 ICE 自动选择 host、srflx 或 relay。"
      />

      <a-card title="Coturn 运行状态" class="mb-4">
        <a-descriptions :column="3" bordered size="small">
          <a-descriptions-item label="状态">
            <a-badge :status="status?.running ? 'success' : status?.enabled ? 'error' : 'default'" :text="statusText" />
          </a-descriptions-item>
          <a-descriptions-item label="PID">{{ status?.pid || '-' }}</a-descriptions-item>
          <a-descriptions-item label="配置 revision">{{ status?.revision || '-' }}</a-descriptions-item>
          <a-descriptions-item label="监听地址">{{ status ? `${status.listen_ip}:${status.port}` : '-' }}</a-descriptions-item>
          <a-descriptions-item label="公网地址">{{ status?.public_host || '-' }}</a-descriptions-item>
          <a-descriptions-item label="Relay 端口">{{ status ? `${status.relay_min_port}-${status.relay_max_port}` : '-' }}</a-descriptions-item>
        </a-descriptions>
        <a-alert v-if="status?.last_error" class="mt-3" type="error" show-icon :message="status.last_error" />
      </a-card>

      <template v-if="config">
        <a-card title="连接策略" class="mb-4">
          <a-form layout="vertical">
            <a-form-item label="启用 Panel 直达探测">
              <a-switch v-model:checked="config.direct_probe_enabled" />
              <span class="ml-3 muted">测试阶段保持关闭，所有 RTC 连接固定进入标准 net_rtc；稳定后再开启。</span>
            </a-form-item>
          </a-form>
        </a-card>

        <a-card title="Console 内置 Coturn" class="mb-4">
          <a-form layout="vertical">
            <a-row :gutter="16">
              <a-col :span="6"><a-form-item label="启用"><a-switch v-model:checked="config.managed_console_server.enabled" /></a-form-item></a-col>
              <a-col :span="9"><a-form-item label="监听 IP"><a-input v-model:value="config.managed_console_server.listen_ip" /></a-form-item></a-col>
              <a-col :span="9"><a-form-item label="对外公布地址"><a-input v-model:value="config.managed_console_server.public_host" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="STUN/TURN 端口"><a-input-number v-model:value="config.managed_console_server.port" :min="1" :max="65535" class="w-full" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="Relay 起始端口"><a-input-number v-model:value="config.managed_console_server.relay_min_port" :min="1" :max="65535" class="w-full" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="Relay 结束端口"><a-input-number v-model:value="config.managed_console_server.relay_max_port" :min="1" :max="65535" class="w-full" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="凭据有效期（秒）"><a-input-number v-model:value="config.managed_console_server.credential_ttl_seconds" :min="60" :max="3600" class="w-full" /></a-form-item></a-col>
              <a-col :span="12"><a-form-item label="Realm"><a-input v-model:value="config.managed_console_server.realm" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="UDP"><a-switch v-model:checked="config.managed_console_server.enable_udp" /></a-form-item></a-col>
              <a-col :span="6"><a-form-item label="TCP"><a-switch v-model:checked="config.managed_console_server.enable_tcp" /></a-form-item></a-col>
            </a-row>
          </a-form>
        </a-card>

        <a-card class="mb-4">
          <template #title>附加 STUN/TURN Server（{{ editors.length }}/8）</template>
          <template #extra><a-button type="primary" :disabled="editors.length >= 8" @click="addServer">新增 Server</a-button></template>
          <a-empty v-if="editors.length === 0" description="当前只使用 Console 内置 Coturn" />
          <a-card v-for="(server, index) in editors" :key="server.id" size="small" class="server-card">
            <template #title>{{ server.name || server.id }}</template>
            <template #extra><a-button danger type="link" @click="editors.splice(index, 1)">删除</a-button></template>
            <a-form layout="vertical">
              <a-row :gutter="16">
                <a-col :span="3"><a-form-item label="启用"><a-switch v-model:checked="server.enabled" /></a-form-item></a-col>
                <a-col :span="7"><a-form-item label="ID"><a-input v-model:value="server.id" /></a-form-item></a-col>
                <a-col :span="7"><a-form-item label="名称"><a-input v-model:value="server.name" /></a-form-item></a-col>
                <a-col :span="7"><a-form-item label="凭据模式"><a-select v-model:value="server.credential_mode" :options="[{value:'none',label:'无（仅 STUN）'},{value:'static',label:'固定用户名/密码'}]" /></a-form-item></a-col>
                <a-col :span="12"><a-form-item label="ICE URL（每行一个，最多 4 个）"><a-textarea v-model:value="server.urls_text" :rows="4" /></a-form-item></a-col>
                <a-col :span="6"><a-form-item label="用户名"><a-input v-model:value="server.username" :disabled="server.credential_mode !== 'static'" /></a-form-item></a-col>
                <a-col :span="6"><a-form-item label="密码"><a-input-password v-model:value="server.credential" :disabled="server.credential_mode !== 'static'" /></a-form-item></a-col>
              </a-row>
            </a-form>
          </a-card>
        </a-card>

        <div class="actions">
          <a-button @click="refresh">重新加载</a-button>
          <a-button @click="restoreDefaults">恢复默认值</a-button>
          <a-button :loading="testing" @click="validate">测试配置</a-button>
          <a-button type="primary" :loading="saving" @click="save">保存并应用</a-button>
        </div>
      </template>
    </a-spin>
  </div>
</template>

<style scoped>
.rtc-page { padding: 20px; }
.server-card { margin-bottom: 12px; }
.actions { display: flex; justify-content: flex-end; gap: 10px; padding-bottom: 24px; }
.muted { color: #8c8c8c; }
.w-full { width: 100%; }
.mb-4 { margin-bottom: 16px; }
.mt-3 { margin-top: 12px; }
.ml-3 { margin-left: 12px; }
</style>
