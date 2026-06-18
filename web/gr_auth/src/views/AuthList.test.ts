import { flushPromises, mount } from '@vue/test-utils'
import { createPinia } from 'pinia'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import http from '@/utils/http'
import AuthList from './AuthList.vue'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

const now = 1_700_000_000_000

const authItem = (overrides = {}) => ({
  auth_id: 'auth-a',
  auth_name: 'customer-a',
  machine_code: 'machine-a',
  description: '',
  max_streams: 4,
  appkey: 'appkey-a',
  app_secret: 'secret-a',
  username: 'user-a',
  password: 'password-a',
  created_timestamp_ms: now - 2 * 24 * 3600 * 1000,
  end_timestamp_ms: now + 28 * 24 * 3600 * 1000,
  days: 30,
  verify_server: 'https://verify.example',
  deploy_str: 'deploy-info',
  role: 1,
  total: 42,
  disable_modify: false,
  left_days: 0,
  ...overrides,
})

const mountAuthList = () => mount(AuthList, {
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
        template: '<div><slot name="header" /><slot /></div>',
      },
      ElPagination: {
        props: ['currentPage', 'pageSize', 'total'],
        emits: ['update:currentPage', 'update:pageSize', 'size-change', 'current-change'],
        template: '<div />',
      },
      ElForm: { template: '<form><slot /></form>' },
      ElFormItem: { template: '<label><slot /></label>' },
      ElInput: {
        props: ['modelValue'],
        emits: ['update:modelValue'],
        template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" @keyup.enter="$emit(\'keyup.enter\')" />',
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

type AuthListVm = ReturnType<typeof mountAuthList>['vm'] & Record<string, any>

const vmOf = (wrapper: ReturnType<typeof mountAuthList>) => wrapper.vm as AuthListVm

describe('AuthList', () => {
  beforeEach(() => {
    vi.useFakeTimers()
    vi.setSystemTime(now)
    vi.mocked(http.get).mockReset()
    vi.mocked(http.post).mockReset()
  })

  it('loads authorizations on mount and uses backend total', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: [authItem()],
      },
    })

    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    expect(http.get).toHaveBeenCalledWith('/query/authorizations', {
      params: {
        page: 1,
        page_size: 20,
      },
    })
    expect(vm.tableData).toHaveLength(1)
    expect(vm.totalAuthCount).toBe(42)
    expect(vm.tableData[0].left_days).toBe(28)
  })

  it('clears table and shows fallback error when loading fails', async () => {
    vi.mocked(http.get).mockRejectedValue({})

    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    expect(vm.tableData).toEqual([])
    expect(vm.totalAuthCount).toBe(0)
    expect(vm.errorDialogVisible).toBe(true)
    expect(vm.errorMessage).toBe('请求授权列表失败')
  })

  it('searches by name and uses search endpoint params', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ auth_id: 'auth-b', total: 1 })] } })
    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.search = 'customer-b'
    vm.handleSearch()
    await flushPromises()

    expect(http.get).toHaveBeenLastCalledWith('/query/authorization/like/name', {
      params: {
        page: 1,
        page_size: 10,
        auth_name: 'customer-b',
      },
    })
    expect(vm.tableData[0].auth_id).toBe('auth-b')
    expect(vm.totalAuthCount).toBe(1)
  })

  it('reloads current page when search is empty', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ auth_id: 'auth-c', total: 2 })] } })
    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.currentPage = 3
    vm.pageSize = 40
    vm.search = ''
    vm.handleSearch()
    await flushPromises()

    expect(http.get).toHaveBeenLastCalledWith('/query/authorizations', {
      params: {
        page: 3,
        page_size: 40,
      },
    })
    expect(vm.tableData[0].auth_id).toBe('auth-c')
  })

  it('reloads using updated pagination values', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ auth_id: 'auth-page-size' })] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ auth_id: 'auth-page' })] } })
    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    await vm.handleSizeChange(60)
    await flushPromises()

    expect(http.get).toHaveBeenLastCalledWith('/query/authorizations', {
      params: {
        page: 1,
        page_size: 60,
      },
    })

    await vm.handleCurrentChange(4)
    await flushPromises()

    expect(http.get).toHaveBeenLastCalledWith('/query/authorizations', {
      params: {
        page: 4,
        page_size: 60,
      },
    })
  })

  it('saves normalized authorization update and refreshes current page', async () => {
    vi.mocked(http.get)
      .mockResolvedValueOnce({ data: { data: [authItem()] } })
      .mockResolvedValueOnce({ data: { data: [authItem({ days: 7, max_streams: 2, role: 2 })] } })
    vi.mocked(http.post).mockResolvedValue({ data: { code: 200 } })
    const wrapper = mountAuthList()
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

    expect(http.post).toHaveBeenCalledWith('/update/authorization', {
      auth_id: 'auth-a',
      days: 7,
      max_streams: 2,
      role: 2,
    })
    expect(http.get).toHaveBeenLastCalledWith('/query/authorizations', {
      params: {
        page: 2,
        page_size: 40,
      },
    })
    expect(vm.errorMessage).toBe('修改成功')
    expect(vm.dialogVisible).toBe(false)
  })

  it('does not call update API when selected authorization is invalid', async () => {
    vi.mocked(http.get).mockResolvedValue({ data: { data: [authItem()] } })
    const wrapper = mountAuthList()
    await flushPromises()
    const vm = vmOf(wrapper)

    vm.handleModifyInfo({
      ...authItem(),
      days: 0,
    })
    await vm.handleSave()
    await flushPromises()

    expect(http.post).not.toHaveBeenCalled()
    expect(vm.errorDialogVisible).toBe(true)
    expect(vm.errorMessage).toContain('Days 必须在 1 到')
  })
})
