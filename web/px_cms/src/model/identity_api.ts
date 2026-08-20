import axiosHttp from '@/http'

export interface GroupRef { gid: string; name: string }
export interface UserAdminView {
  uid: string; username: string; avatar_url: string; assigned: boolean; disabled: boolean
  auth_version: number; must_change_password: boolean; groups: GroupRef[]
  created_at: number; updated_at: number; version: number
}
export interface GroupView {
  gid: string; name: string; remark: string; member_count: number; device_count: number
  app_count: number; created_at: number; updated_at: number; version: number
}
export interface DeviceOption { device_id: string; name: string; online: boolean }
export interface AppOption { app_id: string; name: string; access_mode: 'public' | 'acl'; version: number }
export interface UserSessionView {
  sid: string; client_type: string; created_at: number; last_used_at: number
  expires_at: number; absolute_expires_at: number; revoked_at?: number
  ip_hash_prefix: string; user_agent_hash_prefix: string
}
export interface GuestSessionView {
  sid: string; guest_id: string; client_type: string; created_at: number; last_used_at: number
  expires_at: number; revoked_at?: number; ip_hash_prefix: string; user_agent_hash_prefix: string
}

const unwrap = <T>(response: { data: { data: T } }) => response.data.data
export async function listAdminUsers(page = 1, keyword = '', pageSize = 20) {
  return unwrap<{ items: UserAdminView[]; total: number }>(await axiosHttp.get('/api/v1/admin/users', { params: { page, page_size: pageSize, keyword } }))
}
export async function createAdminUser(request: { username: string; initial_password?: string; group_ids: string[]; device_ids: string[] }) {
  return unwrap<{ user: UserAdminView; initial_password: string }>(await axiosHttp.post('/api/v1/admin/users', request))
}
export async function batchCreateAdminUsers(request: { size: number; username_prefix: string; group_ids: string[] }) {
  const response = await axiosHttp.post('/api/v1/admin/users/batch.csv', request, { responseType: 'blob' })
  return response.data as Blob
}
export async function patchAdminUser(uid: string, request: { version: number; username?: string; disabled?: boolean; group_ids?: string[]; device_ids?: string[] }) {
  return unwrap<UserAdminView>(await axiosHttp.patch(`/api/v1/admin/users/${encodeURIComponent(uid)}`, request))
}
export async function deleteAdminUser(user: UserAdminView) {
  return unwrap<UserAdminView>(await axiosHttp.delete(`/api/v1/admin/users/${encodeURIComponent(user.uid)}`, { data: { version: user.version } }))
}
export async function resetAdminUserPassword(user: UserAdminView) {
  return unwrap<{ user: UserAdminView; initial_password: string }>(await axiosHttp.post(`/api/v1/admin/users/${encodeURIComponent(user.uid)}/password/reset`, { version: user.version, generated: true }))
}
export async function revokeAdminUserSessions(uid: string) {
  return unwrap<UserAdminView>(await axiosHttp.post(`/api/v1/admin/users/${encodeURIComponent(uid)}/sessions/revoke-all`, {}))
}
export async function listAdminUserSessions(uid: string) {
  return unwrap<UserSessionView[]>(await axiosHttp.get(`/api/v1/admin/users/${encodeURIComponent(uid)}/sessions`))
}
export async function listGuestSessions() {
  return unwrap<GuestSessionView[]>(await axiosHttp.get('/api/v1/admin/guest-sessions'))
}
export async function blockGuestSession(sid: string, blockGuestId: boolean, blockIpHash: boolean) {
  return unwrap<boolean>(await axiosHttp.post(`/api/v1/admin/guest-sessions/${encodeURIComponent(sid)}/block`, {
    block_guest_id: blockGuestId,
    block_ip_hash: blockIpHash,
    reason: 'admin_console',
  }))
}
export async function listUserPersonalDevices(uid: string) {
  return unwrap<string[]>(await axiosHttp.get(`/api/v1/admin/users/${encodeURIComponent(uid)}/devices`))
}
export async function replaceUserPersonalDevices(uid: string, deviceIds: string[]) {
  return unwrap<string[]>(await axiosHttp.put(`/api/v1/admin/users/${encodeURIComponent(uid)}/devices`, { device_ids: deviceIds }))
}
export async function listGroups() { return unwrap<GroupView[]>(await axiosHttp.get('/api/v1/admin/groups')) }
export async function createGroup(name: string, remark: string) { return unwrap<GroupView>(await axiosHttp.post('/api/v1/admin/groups', { name, remark })) }
export async function patchGroup(group: GroupView, name: string, remark: string) { return unwrap<GroupView>(await axiosHttp.patch(`/api/v1/admin/groups/${group.gid}`, { version: group.version, name, remark })) }
export async function deleteGroup(group: GroupView) { return unwrap<boolean>(await axiosHttp.delete(`/api/v1/admin/groups/${group.gid}`, { data: { version: group.version } })) }
export async function groupIds(kind: 'members'|'devices'|'apps', gid: string) { return unwrap<string[]>(await axiosHttp.get(`/api/v1/admin/groups/${gid}/${kind}`)) }
export async function replaceGroupIds(kind: 'members'|'devices'|'apps', group: GroupView, ids: string[]) {
  const field = kind === 'members' ? 'user_ids' : kind === 'devices' ? 'device_ids' : 'app_ids'
  return unwrap<GroupView>(await axiosHttp.put(`/api/v1/admin/groups/${group.gid}/${kind}`, { version: group.version, [field]: ids }))
}
export async function listDeviceOptions() { return unwrap<DeviceOption[]>(await axiosHttp.get('/api/v1/admin/catalog/devices')) }
export async function listAppOptions() { return unwrap<AppOption[]>(await axiosHttp.get('/api/v1/admin/catalog/apps')) }
export async function updateAppAccess(app: AppOption, access_mode: 'public'|'acl') { return unwrap<AppOption>(await axiosHttp.patch(`/api/v1/admin/apps/${app.app_id}/access`, { version: app.version, access_mode })) }
export async function createInvite(group_ids: string[], lifetime_minutes = 1440) {
  return unwrap<{ invite_code: string; expires_at: number; group_ids: string[] }>(await axiosHttp.post('/api/v1/admin/invites', { group_ids, lifetime_minutes }))
}
