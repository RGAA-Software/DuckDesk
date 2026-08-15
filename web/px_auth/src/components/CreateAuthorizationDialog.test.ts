import { flushPromises, mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import { useAuthStore } from '@/stores/auth'
import http from '@/utils/http'
import CreateAuthorizationDialog from './CreateAuthorizationDialog.vue'

const mocks = vi.hoisted(() => ({
  messageSuccess: vi.fn(),
  messageError: vi.fn(),
}))

vi.mock('@/utils/http', () => ({
  default: {
    post: vi.fn(),
  },
}))

vi.mock('element-plus', () => ({
  ElMessage: {
    success: mocks.messageSuccess,
    error: mocks.messageError,
  },
}))

const mountDialog = () => {
  const pinia = createPinia()
  setActivePinia(pinia)
  const wrapper = mount(CreateAuthorizationDialog, {
    props: {
      modelValue: true,
    },
    global: {
      plugins: [pinia],
      stubs: {
        ElDialog: {
          props: ['modelValue'],
          template: '<div v-if="modelValue"><slot /><slot name="footer" /></div>',
        },
        Warning: { template: '<span />' },
        ElIcon: { template: '<span><slot /></span>' },
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
  return { wrapper, store: useAuthStore() }
}

type DialogVm = ReturnType<typeof mountDialog>['wrapper']['vm'] & Record<string, any>

const vmOf = (wrapper: ReturnType<typeof mountDialog>['wrapper']) => wrapper.vm as DialogVm

const fillValidForm = (vm: DialogVm) => {
  vm.form = {
    name: ' customer-a ',
    machine_code: ' machine-a ',
    days: '30',
    max_streams: '4',
  }
}

describe('CreateAuthorizationDialog', () => {
  beforeEach(() => {
    vi.mocked(http.post).mockReset()
    mocks.messageSuccess.mockReset()
    mocks.messageError.mockReset()
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: vi.fn().mockResolvedValue(undefined),
      },
    })
    vi.stubGlobal('URL', {
      createObjectURL: vi.fn(() => 'blob:deploy-info'),
      revokeObjectURL: vi.fn(),
    })
  })

  it('creates authorization, shows deploy info, closes form, and refreshes list', async () => {
    vi.mocked(http.post).mockResolvedValue({
      data: {
        data: 'deploy-info',
      },
    })
    const { wrapper, store } = mountDialog()
    const vm = vmOf(wrapper)
    fillValidForm(vm)

    await vm.handleCreate()
    await flushPromises()

    expect(http.post).toHaveBeenCalledWith('/create/new/deploy/authorization', {
      name: 'customer-a',
      machine_code: 'machine-a',
      days: 30,
      max_streams: 4,
    })
    expect(vm.deployInfo).toBe('deploy-info')
    expect(vm.deployDialogVisible).toBe(true)
    expect(wrapper.emitted('update:modelValue')?.[0]).toEqual([false])
    expect(store.refreshFlag).toBe(1)
    expect(vm.errorMessage).toBe('创建成功')
  })

  it('does not call create API for invalid input', async () => {
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    vm.form = {
      name: '',
      machine_code: 'machine-a',
      days: '30',
      max_streams: '4',
    }

    await vm.handleCreate()
    await flushPromises()

    expect(http.post).not.toHaveBeenCalled()
    expect(vm.errorDialogVisible).toBe(true)
    expect(vm.errorMessage).toBe('请填写完整授权信息')
  })

  it('shows backend error when create fails', async () => {
    vi.mocked(http.post).mockRejectedValue({
      response: {
        data: {
          message: '授权已存在',
        },
      },
    })
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    fillValidForm(vm)

    await vm.handleCreate()
    await flushPromises()

    expect(vm.errorDialogVisible).toBe(true)
    expect(vm.errorMessage).toBe('授权已存在')
    expect(vm.deployDialogVisible).toBe(false)
  })

  it('ignores repeated create while request is pending', async () => {
    let resolveCreate!: (value: any) => void
    vi.mocked(http.post).mockReturnValue(new Promise((resolve) => {
      resolveCreate = resolve
    }))
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    fillValidForm(vm)

    const firstCreate = vm.handleCreate()
    const secondCreate = vm.handleCreate()

    expect(http.post).toHaveBeenCalledTimes(1)

    resolveCreate({ data: { data: 'deploy-info' } })
    await firstCreate
    await secondCreate
    await flushPromises()

    expect(vm.deployInfo).toBe('deploy-info')
  })

  it('copies deploy information to clipboard', async () => {
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    vm.deployInfo = 'deploy-info'

    await vm.copyDeployInfo()

    expect(navigator.clipboard.writeText).toHaveBeenCalledWith('deploy-info')
    expect(mocks.messageSuccess).toHaveBeenCalledWith('已复制到剪贴板')
  })

  it('shows copy error when clipboard write fails', async () => {
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: vi.fn().mockRejectedValue(new Error('clipboard denied')),
      },
    })
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    vm.deployInfo = 'deploy-info'

    await vm.copyDeployInfo()

    expect(navigator.clipboard.writeText).toHaveBeenCalledWith('deploy-info')
    expect(mocks.messageError).toHaveBeenCalledWith('复制失败')
  })

  it('downloads deploy information as auth info file', () => {
    const click = vi.fn()
    const { wrapper } = mountDialog()
    const vm = vmOf(wrapper)
    vi.spyOn(document, 'createElement').mockReturnValue({
      click,
      href: '',
      download: '',
    } as unknown as HTMLAnchorElement)
    vm.deployInfo = 'deploy-info'
    vm.form.name = 'customer-a'

    vm.downloadDeployInfo()

    expect(URL.createObjectURL).toHaveBeenCalled()
    expect(click).toHaveBeenCalled()
    expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:deploy-info')
  })
})
