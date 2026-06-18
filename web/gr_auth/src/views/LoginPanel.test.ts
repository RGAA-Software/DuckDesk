import { flushPromises, mount } from '@vue/test-utils'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import http from '@/utils/http'
import LoginPanel from './LoginPanel.vue'

const mocks = vi.hoisted(() => ({
  routerPush: vi.fn(),
}))

vi.mock('@/utils/http', () => ({
  default: {
    post: vi.fn(),
  },
}))

vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: mocks.routerPush,
  }),
}))

const mountLoginPanel = () => mount(LoginPanel, {
  global: {
    stubs: {
      ElRow: { template: '<div><slot /></div>' },
      ElCol: { template: '<div><slot /></div>' },
      ElCard: { template: '<section><slot name="header" /><slot /></section>' },
      ElDialog: {
        props: ['modelValue'],
        template: '<div v-if="modelValue"><slot /><slot name="footer" /></div>',
      },
      ElIcon: { template: '<span><slot /></span>' },
      ElInput: {
        props: ['modelValue'],
        emits: ['update:modelValue'],
        template: '<input :value="modelValue" @input="$emit(\'update:modelValue\', $event.target.value)" />',
      },
      ElButton: {
        emits: ['click'],
        template: '<button @click="$emit(\'click\')"><slot /></button>',
      },
    },
  },
})

describe('LoginPanel', () => {
  beforeEach(() => {
    sessionStorage.clear()
    mocks.routerPush.mockReset()
    vi.mocked(http.post).mockReset()
  })

  it('stores token and navigates to main page on successful login', async () => {
    vi.mocked(http.post).mockResolvedValue({
      data: {
        code: 200,
        data: {
          token: 'login-token-a',
        },
      },
    })
    const wrapper = mountLoginPanel()

    const inputs = wrapper.findAll('input')
    await inputs[0].setValue('Admin')
    await inputs[1].setValue('password-a')
    const buttons = wrapper.findAll('button')
    await buttons[buttons.length - 1].trigger('click')
    await flushPromises()

    expect(http.post).toHaveBeenCalledWith('/verify/author', {
      author_name: 'Admin',
      author_token: 'password-a',
    })
    expect(sessionStorage.getItem('login_token')).toBe('login-token-a')
    expect(mocks.routerPush).toHaveBeenCalledWith('/main')
    expect(wrapper.text()).not.toContain('用户名或密码错误')
  })

  it('does not store token when backend returns business failure', async () => {
    vi.mocked(http.post).mockResolvedValue({
      data: {
        code: 801,
        message: '账号或密码错误',
      },
    })
    const wrapper = mountLoginPanel()

    await wrapper.findAll('input')[0].setValue('Admin')
    await wrapper.findAll('input')[1].setValue('bad-password')
    const buttons = wrapper.findAll('button')
    await buttons[buttons.length - 1].trigger('click')
    await flushPromises()

    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(mocks.routerPush).not.toHaveBeenCalled()
    expect(wrapper.text()).toContain('账号或密码错误')
  })

  it('shows fallback error and keeps token empty when request fails', async () => {
    vi.mocked(http.post).mockRejectedValue({})
    const wrapper = mountLoginPanel()

    await wrapper.findAll('input')[0].setValue('Admin')
    await wrapper.findAll('input')[1].setValue('bad-password')
    const buttons = wrapper.findAll('button')
    await buttons[buttons.length - 1].trigger('click')
    await flushPromises()

    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(mocks.routerPush).not.toHaveBeenCalled()
    expect(wrapper.text()).toContain('用户名或密码错误')
  })
})
