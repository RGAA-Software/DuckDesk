<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { Modal, message, type FormInstance } from 'ant-design-vue'
import { copyText } from '@/util/clipboard'
import { validateAdminUsername, validateInitialPassword } from '@/util/identity_validation'
import { batchCreateAdminUsers, blockGuestSession, createAdminUser, deleteAdminUser, listAdminUsers, listAdminUserSessions, listDeviceOptions, listGroups, listGuestSessions, listUserPersonalDevices, patchAdminUser, resetAdminUserPassword, revokeAdminUserSessions, viewAdminUserPassword, type DeviceOption, type GuestSessionView, type GroupView, type UserAdminView, type UserSessionView } from '@/model/identity_api'

const users = ref<UserAdminView[]>([]), groups = ref<GroupView[]>([])
const devices = ref<DeviceOption[]>([])
const total = ref(0), page = ref(1), keyword = ref(''), loading = ref(false), editorOpen = ref(false)
const editorFormRef = ref<FormInstance>(), editorSaving = ref(false)
const editing = ref<UserAdminView>()
const form = reactive({ username: '', password: '', group_ids: [] as string[], device_ids: [] as string[] })
const editorRules = {
  username: [{
    validator: (_rule: unknown, value: string) => {
      const error = validateAdminUsername(value || '')
      return error ? Promise.reject(new Error(error)) : Promise.resolve()
    },
    trigger: ['blur', 'change'],
  }],
  password: [{
    validator: (_rule: unknown, value: string) => {
      const error = validateInitialPassword(value || '')
      return error ? Promise.reject(new Error(error)) : Promise.resolve()
    },
    trigger: ['blur', 'change'],
  }],
}
const batchOpen = ref(false), batchLoading = ref(false)
const sessionsOpen = ref(false), sessionsLoading = ref(false), sessionUser = ref<UserAdminView>()
const sessions = ref<UserSessionView[]>([])
const guestOpen = ref(false), guestLoading = ref(false), guests = ref<GuestSessionView[]>([])
const batchForm = reactive({ size: 10, username_prefix: 'user', group_ids: [] as string[] })
async function refresh() { loading.value = true; try { const [r, gs, ds] = await Promise.all([listAdminUsers(page.value, keyword.value), listGroups(), listDeviceOptions()]); users.value = r.items; total.value = r.total; groups.value = gs; devices.value = ds } finally { loading.value = false } }
function create() { editing.value = undefined; Object.assign(form, { username: '', password: '', group_ids: [], device_ids: [] }); editorFormRef.value?.clearValidate(); editorOpen.value = true }
async function edit(user: UserAdminView) { editing.value = user; Object.assign(form, { username: user.username, password: '', group_ids: user.groups.map(g => g.gid), device_ids: await listUserPersonalDevices(user.uid) }); editorOpen.value = true }
function userRequestError(error: any): string {
  const code = error?.response?.data?.code
  if (code === 608) return '用户名已存在，请更换用户名'
  if (code === 634) return '所选用户组已不存在，请刷新后重试'
  if (code === 602) return '所选设备已不存在，请刷新后重试'
  if (code === 635) return '用户信息已被其他操作修改，请刷新后重试'
  if (code === 633) return '当前账号没有修改用户的权限'
  if (code === 600 || code === 603) return '用户名或密码不符合要求'
  return error?.response?.data?.message || error?.message || '保存用户失败'
}
async function save() {
  try { await editorFormRef.value?.validate() } catch { return }
  editorSaving.value = true
  try {
    if (editing.value) {
      await patchAdminUser(editing.value.uid, { version: editing.value.version, username: form.username, group_ids: form.group_ids, device_ids: form.device_ids })
    } else {
      const r = await createAdminUser({ username: form.username, initial_password: form.password || undefined, group_ids: form.group_ids, device_ids: form.device_ids })
      Modal.info({ title: '用户密码', content: r.initial_password, okText: '关闭' })
    }
    editorOpen.value = false
    await refresh()
  } catch (error) {
    message.error(userRequestError(error))
  } finally {
    editorSaving.value = false
  }
}
async function toggle(user: UserAdminView) { await patchAdminUser(user.uid, { version: user.version, disabled: !user.disabled }); await refresh() }
async function reset(user: UserAdminView) { const r = await resetAdminUserPassword(user); await copyText(r.initial_password); Modal.info({ title: '新密码已复制', content: r.initial_password }); await refresh() }
async function showPassword(user: UserAdminView) {
  try {
    const result = await viewAdminUserPassword(user.uid)
    if (!result.password) {
      Modal.warning({ title: '无法查看历史密码', content: '该账号创建于密码查看功能启用之前，请重置一次密码。' })
      return
    }
    Modal.info({ title: `${user.username} 的当前密码`, content: result.password, okText: '关闭' })
  } catch (error: any) {
    message.error(error?.response?.data?.message || '读取密码失败')
  }
}
async function revoke(user: UserAdminView) { await revokeAdminUserSessions(user.uid); message.success('全部会话已撤销'); await refresh() }
async function showSessions(user: UserAdminView) { sessionUser.value = user; sessionsOpen.value = true; sessionsLoading.value = true; try { sessions.value = await listAdminUserSessions(user.uid) } finally { sessionsLoading.value = false } }
function formatTime(value?: number) { return value ? new Date(value).toLocaleString() : '-' }
async function showGuests() { guestOpen.value = true; guestLoading.value = true; try { guests.value = await listGuestSessions() } finally { guestLoading.value = false } }
function blockGuest(guest: GuestSessionView, includeIp: boolean) { Modal.confirm({ title: includeIp ? '封禁该来源？' : '封禁该访客？', content: includeIp ? `将撤销来源 ${guest.ip_hash_prefix} 的全部访客会话，并拒绝该来源新建访客会话。` : '将立即撤销该访客的全部会话。', okType: 'danger', async onOk() { await blockGuestSession(guest.sid, true, includeIp); message.success('封禁已生效'); guests.value = await listGuestSessions() } }) }
function remove(user: UserAdminView) { Modal.confirm({ title: `删除用户 ${user.username}？`, content: '账号将软删除且全部会话立即失效。', okType: 'danger', async onOk() { await deleteAdminUser(user); await refresh() } }) }
function onTableChange(pagination: { current?: number }) { page.value = pagination.current || 1; void refresh() }
async function generateBatch() {
  batchLoading.value = true
  try {
    const blob = await batchCreateAdminUsers(batchForm)
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `px_users_${new Date().toISOString().slice(0, 10)}.csv`
    link.click()
    URL.revokeObjectURL(url)
    batchOpen.value = false
    message.success('用户已创建；密码已下载，也可随时在用户管理中查看')
    await refresh()
  } finally { batchLoading.value = false }
}
onMounted(refresh)
</script>
<template>
  <a-card title="用户管理">
    <template #extra><a-space><a-input-search v-model:value="keyword" placeholder="用户名" @search="refresh" /><a-button @click="showGuests">访客会话</a-button><a-button @click="batchOpen = true">批量创建</a-button><a-button type="primary" @click="create">新建用户</a-button></a-space></template>
    <a-table :data-source="users" row-key="uid" :loading="loading" :pagination="{ current: page, total, pageSize: 20 }" @change="onTableChange">
      <a-table-column title="用户名" data-index="username" />
      <a-table-column title="用户组"><template #default="{ record }"><a-tag v-for="g in record.groups" :key="g.gid">{{ g.name }}</a-tag></template></a-table-column>
      <a-table-column title="状态"><template #default="{ record }"><a-tag :color="record.disabled ? 'red' : 'green'">{{ record.disabled ? '已禁用' : '正常' }}</a-tag></template></a-table-column>
      <a-table-column title="操作" width="540"><template #default="{ record }"><a-space wrap><a-button size="small" @click="edit(record)">编辑</a-button><a-button size="small" @click="toggle(record)">{{ record.disabled ? '启用' : '禁用' }}</a-button><a-button size="small" @click="showPassword(record)">查看密码</a-button><a-button size="small" @click="reset(record)">重置密码</a-button><a-button size="small" @click="showSessions(record)">会话</a-button><a-button size="small" @click="revoke(record)">撤销会话</a-button><a-button size="small" danger @click="remove(record)">删除</a-button></a-space></template></a-table-column>
    </a-table>
  </a-card>
  <a-modal v-model:open="editorOpen" :title="editing ? '编辑用户' : '新建用户'" :confirm-loading="editorSaving" ok-text="保存" cancel-text="取消" @ok="save">
    <a-form ref="editorFormRef" :model="form" :rules="editorRules" layout="vertical"><a-form-item label="用户名" name="username"><a-input v-model:value="form.username" :maxlength="64" placeholder="3–64 个字符" /></a-form-item><a-form-item v-if="!editing" label="初始密码" name="password" extra="留空将自动生成安全密码；手动设置需输入 8–128 个字符"><a-input-password v-model:value="form.password" :maxlength="128" /></a-form-item><a-form-item label="用户组"><a-select v-model:value="form.group_ids" mode="multiple" :options="groups.map(g => ({ label: g.name, value: g.gid }))" /></a-form-item><a-form-item label="个人设备授权"><a-select v-model:value="form.device_ids" mode="multiple" :options="devices.map(d => ({ label: `${d.name || d.device_id}（${d.online ? '在线' : '离线'}）`, value: d.device_id }))" /></a-form-item></a-form>
  </a-modal>
  <a-modal v-model:open="batchOpen" title="批量创建用户" :confirm-loading="batchLoading" @ok="generateBatch">
    <a-alert type="info" show-icon message="密码会写入本次 CSV，也可由 CMS 管理员随后逐个查看。" />
    <a-form layout="vertical" style="margin-top: 16px"><a-form-item label="用户名开头"><a-input v-model:value="batchForm.username_prefix" :maxlength="48" /></a-form-item><a-form-item label="数量（最多 500）"><a-input-number v-model:value="batchForm.size" :min="1" :max="500" /></a-form-item><a-form-item label="用户组"><a-select v-model:value="batchForm.group_ids" mode="multiple" :options="groups.map(g => ({ label: g.name, value: g.gid }))" /></a-form-item></a-form>
  </a-modal>
  <a-modal v-model:open="sessionsOpen" :title="`${sessionUser?.username || ''} 的会话`" :footer="null" width="920px">
    <a-alert type="info" show-icon message="仅显示来源哈希前缀用于关联排查，不暴露原始 IP、User-Agent 或令牌。" style="margin-bottom: 12px" />
    <a-table :data-source="sessions" row-key="sid" :loading="sessionsLoading" :pagination="false" size="small" :scroll="{ x: 820 }">
      <a-table-column title="客户端" data-index="client_type" width="110" />
      <a-table-column title="创建时间" width="175"><template #default="{ record }">{{ formatTime(record.created_at) }}</template></a-table-column>
      <a-table-column title="最后使用" width="175"><template #default="{ record }">{{ formatTime(record.last_used_at) }}</template></a-table-column>
      <a-table-column title="来源" width="110"><template #default="{ record }">{{ record.ip_hash_prefix || '-' }}</template></a-table-column>
      <a-table-column title="状态" width="100"><template #default="{ record }"><a-tag :color="record.revoked_at ? 'default' : record.expires_at <= Date.now() ? 'orange' : 'green'">{{ record.revoked_at ? '已撤销' : record.expires_at <= Date.now() ? '已过期' : '有效' }}</a-tag></template></a-table-column>
    </a-table>
  </a-modal>
  <a-modal v-model:open="guestOpen" title="访客会话与来源封禁" :footer="null" width="1080px">
    <a-alert type="warning" show-icon message="来源仅以服务端哈希前缀显示；封禁来源会影响同一公网出口下的其他访客。" style="margin-bottom: 12px" />
    <a-table :data-source="guests" row-key="sid" :loading="guestLoading" :pagination="{ pageSize: 20 }" size="small" :scroll="{ x: 980 }">
      <a-table-column title="访客" data-index="guest_id" width="250" />
      <a-table-column title="客户端" data-index="client_type" width="110" />
      <a-table-column title="来源" data-index="ip_hash_prefix" width="100" />
      <a-table-column title="最后使用" width="175"><template #default="{ record }">{{ formatTime(record.last_used_at) }}</template></a-table-column>
      <a-table-column title="状态" width="90"><template #default="{ record }"><a-tag :color="record.revoked_at ? 'default' : record.expires_at <= Date.now() ? 'orange' : 'green'">{{ record.revoked_at ? '已撤销' : record.expires_at <= Date.now() ? '已过期' : '有效' }}</a-tag></template></a-table-column>
      <a-table-column title="操作" width="190"><template #default="{ record }"><a-space><a-button size="small" danger :disabled="!!record.revoked_at" @click="blockGuest(record, false)">封禁访客</a-button><a-button size="small" danger :disabled="!!record.revoked_at || !record.ip_hash_prefix" @click="blockGuest(record, true)">封禁来源</a-button></a-space></template></a-table-column>
    </a-table>
  </a-modal>
</template>
