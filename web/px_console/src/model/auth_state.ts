import { ref } from 'vue'
import type { Authorization } from '@/entity/authorization.ts'
import { queryAuthorization } from '@/model/auth_api.ts'

// 全局共享的授权信息：HomeView（右上角授权状态）和 ProfileInfo（个人中心）
// 共用同一份，任何一处刷新（如个人中心"刷新授权"）都会同步到其它页面。
export const sharedAuthorization = ref<Authorization | null>(null)

export async function refreshSharedAuthorization() {
  sharedAuthorization.value = await queryAuthorization()
}
