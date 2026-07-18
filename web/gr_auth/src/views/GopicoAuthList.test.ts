import { flushPromises, mount } from '@vue/test-utils'
import { createPinia } from 'pinia'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import http from '@/utils/http'
import GopicoAuthList from './GopicoAuthList.vue'

const mocks = vi.hoisted(() => ({
  messageSuccess: vi.fn(),
  messageError: vi.fn(),
  messageBoxConfirm: vi.fn(),
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

vi.mock('element-plus', () => ({
  ElMessage: {
    success: mocks.messageSuccess,
    error: mocks.messageError,
  },
  ElMessageBox: {
    confirm: mocks.messageBoxConfirm,
  },
}))

const now = 1_700_000_000_000

const authItem = (overrides = {}) => ({
  auth_id: 'auth-a',
  auth_name: 'customer-a',
  machine_code: 'machine-a',
  created_timestamp_ms: now - 2 * 24 * 3600 * 1000,
  end_timestamp_ms: now + 28 * 24 * 3600 * 1000,
  last_modify_timestamp: now,
  days: 30,
  max_streams: 4,
  role: 1,
  verify_server: 'https://verify.example',
  deploy_str: 'deploy-info',
  product: 'gopico',
  revoked: false,
  revoked_at_ms: 0,
  client_version: '1.2.3',
  client_status: 'online',
  client_os: 'android',
  client_device_count: 2,
  client_reported_at_ms: now,
  total: 42,
  ...overrides,
})

const mountGopicoAuthList = () => mount(GopicoAuthList, {
  global: {
    plugins: [createPinia()],
    stubs: {
      ElDialog: {
        props: ['modelValue'],
        template: '<div v-if="modelValue"><slot /><slot name="footer" /></div>',
      },
      ElIcon: { template: '<span><slot /></span>' },
      ElTable: {
        props: ['data'],
        template: '<div><slot /></div>',
      },
      ElTableColumn: {
        template: '<div><slot name="header" /></div>',
      },
      ElPagination: {
        props: ['currentPage', 'pageSize', 'total'],
        emits: ['update:currentPage', 'update:pageSize', 'size-change', 'current-change'],
        template: '<div />',
      },
      ElTag: { template: '<span><slot /></span>' },
      ElForm: { template: '<form><slot /></form>' },
      ElFormItem: { template: '<label><slot /></label>' },
      ElInput: {
        props: ['modelValue'],
        emits: ['update:modelValue'],
        template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
      },
      ElSelect: {
        props: ['modelValue'],
        emits: ['update:modelValue'],
        template: '<select :value="modelValue" @change="$emit(\'update:modelValue\', $event.target.value)"><slot /></select>',
      },
      ElOption: { template: '<option><slot /></option>' },
      ElButton: {
        emits: ['click'],
        template: '<button @click="$emit(\'click\')"><slot /></button>',
      },
    },
  },
})

type GopicoAuthListVm = ReturnType<typeof mountGopicoAuthList>['vm'] & Record<string, any>

const vmOf = (wrapper: ReturnType<typeof mountGopicoAuthList>) => wrapper.vm as GopicoAuthListVm

describe('GopicoAuthList', () => {
  beforeEach(() => {
    vi.useFakeTimers()
    vi.setSystemTime(now)
    vi.mocked(http.get).mockReset()
    vi.mocked(http.post).mockReset()
    mocks.messageSuccess.mockReset()
    mocks.messageError.mockReset()
    mocks.messageBoxConfirm.mockReset()
    mocks.messageBoxConfirm.mockResolvedValue('confirm')
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: vi.fn().mockResolvedValue(undefined),
      },
    })
  })

  it('loads gopico authorizations on mount and uses backend total', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: [authItem()],
      },
    })

    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    expect(http.get).toHaveBeenCalledWith('/gopico/query/authorizations', {
      params: {
        page: 1,
        page_size: 20,
      },
    })
    expect(vm.tableData).toHaveLength(1)
    expect(vm.totalAuthCount).toBe(42)
    expect(vm.tableData[0].client_version).toBe('1.2.3')
    expect(vm.tableData[0].client_status).toBe('online')
    expect(vm.tableData[0].client_device_count).toBe(2)
  })

  it('shows "-" for authorizations that never reported client status', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: [authItem({ client_reported_at_ms: 0 })],
      },
    })

    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    expect(vm.formatReportedTime(vm.tableData[0])).toBe('-')
    expect(vm.formatReportedTime(authItem())).toBe(new Date(now).toLocaleString())
  })

  it('filters table data by auth name locally', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: [
          authItem({ auth_id: 'auth-a', auth_name: 'customer-a' }),
          authItem({ auth_id: 'auth-b', auth_name: 'other-b' }),
        ],
      },
    })

    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    expect(vm.filterTableData).toHaveLength(2)

    vm.search = 'customer'
    expect(vm.filterTableData).toHaveLength(1)
    expect(vm.filterTableData[0].auth_id).toBe('auth-a')
  })

  it('revokes authorization after confirmation and refreshes current page', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ revoked: true })] } })
    vi.mocked(http.post).mockResolvedValue({ data: { code: 200 } })
    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.currentPage = 2
    vm.pageSize = 40
    await vm.handleRevoke(authItem())
    await flushPromises()

    expect(mocks.messageBoxConfirm).toHaveBeenCalled()
    expect(http.post).toHaveBeenCalledWith('/gopico/revoke/authorization', {
      auth_id: 'auth-a',
    })
    expect(http.get).toHaveBeenLastCalledWith('/gopico/query/authorizations', {
      params: {
        page: 2,
        page_size: 40,
      },
    })
    expect(vm.errorMessage).toBe('已吊销')
  })

  it('does not call revoke API when confirmation is cancelled', async () => {
    mocks.messageBoxConfirm.mockRejectedValue(new Error('cancel'))
    vi.mocked(http.get).mockResolvedValue({ data: { data: [authItem()] } })
    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    await vm.handleRevoke(authItem())
    await flushPromises()

    expect(http.post).not.toHaveBeenCalled()
  })

  it('saves normalized authorization update with max_devices and refreshes current page', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ days: 7, max_streams: 2, role: 2 })] } })
    vi.mocked(http.post).mockResolvedValue({ data: { code: 200 } })
    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.currentPage = 2
    vm.pageSize = 40
    vm.handleModifyInfo({
      ...authItem(),
      days: '7',
      max_streams: '2',
      role: '2',
    })
    await vm.handleSave()
    await flushPromises()

    expect(http.post).toHaveBeenCalledWith('/gopico/update/authorization', {
      auth_id: 'auth-a',
      days: 7,
      max_devices: 2,
      role: 2,
    })
    expect(http.get).toHaveBeenLastCalledWith('/gopico/query/authorizations', {
      params: {
        page: 2,
        page_size: 40,
      },
    })
    expect(vm.errorMessage).toBe('修改成功')
    expect(vm.dialogVisible).toBe(false)
  })

  it('creates gopico authorization and shows deploy info dialog', async () => {
    vi.mocked(http.get).mockResolvedValue({ data: { data: [authItem()] } })
    vi.mocked(http.post).mockResolvedValue({
      data: {
        data: 'gopico-deploy',
      },
    })
    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.handleOpenCreate()
    vm.createForm = {
      name: ' customer-b ',
      machine_code: ' machine-b ',
      role: '1',
      days: '30',
      max_devices: '40',
    }
    await vm.handleCreate()
    await flushPromises()

    expect(http.post).toHaveBeenCalledWith('/gopico/create/new/deploy/authorization', {
      name: 'customer-b',
      machine_code: 'machine-b',
      days: 30,
      max_devices: 40,
      role: 1,
    })
    expect(vm.deployInfo).toBe('gopico-deploy')
    expect(vm.createDialogVisible).toBe(false)
    expect(vm.deployDialogVisible).toBe(true)
    expect(vm.errorMessage).toBe('创建成功')
  })

  it('copies deploy string fetched by auth id to clipboard', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: 'gopico-deploy' } })
    const wrapper = mountGopicoAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    await vm.handleCopyDeploy(authItem())
    await flushPromises()

    expect(http.get).toHaveBeenLastCalledWith('/gopico/query/deploy/authorization/by/id', {
      params: {
        auth_id: 'auth-a',
      },
    })
    expect(navigator.clipboard.writeText).toHaveBeenCalledWith('gopico-deploy')
    expect(mocks.messageSuccess).toHaveBeenCalledWith('已复制到剪贴板')
  })
})
