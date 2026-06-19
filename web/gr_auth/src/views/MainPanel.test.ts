import { flushPromises, mount } from '@vue/test-utils'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import http from '@/utils/http'
import MainPanel from './MainPanel.vue'

const mocks = vi.hoisted(() => ({
  routerPush: vi.fn(),
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

vi.mock('@/router', () => ({
  default: {
    push: mocks.routerPush,
  },
}))

vi.mock('vue-router', () => ({
  RouterView: { template: '<div />' },
  useRoute: () => ({
    path: '/main/auth-list',
  }),
}))

vi.mock('@/components/CreateAuthorizationDialog.vue', () => ({
  default: {
    props: ['modelValue'],
    template: '<div />',
  },
}))

const mountMainPanel = () => mount(MainPanel, {
  global: {
    stubs: {
      ElContainer: { template: '<div><slot /></div>' },
      ElHeader: { template: '<header><slot /></header>' },
      ElAside: { template: '<aside><slot /></aside>' },
      ElMain: { template: '<main><slot /></main>' },
      ElText: { template: '<span><slot /></span>' },
      ElMenu: { template: '<nav><slot /></nav>' },
      ElMenuItem: { template: '<a><slot /></a>' },
      ElIcon: { template: '<span><slot /></span>' },
      ElButton: {
        emits: ['click'],
        template: '<button @click="$emit(\'click\')"><slot /></button>',
      },
    },
  },
})

describe('MainPanel', () => {
  beforeEach(() => {
    sessionStorage.clear()
    mocks.routerPush.mockReset()
    vi.mocked(http.get).mockReset()
    vi.mocked(http.post).mockReset()
  })

  it('shows create authorization action for admin users', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: {
          role: 'admin',
        },
      },
    })

    const wrapper = mountMainPanel()
    await flushPromises()

    expect(wrapper.text()).toContain('创建授权')
  })

  it('hides create authorization action for visitor users', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: {
          role: 'visitor',
        },
      },
    })

    const wrapper = mountMainPanel()
    await flushPromises()

    expect(wrapper.text()).not.toContain('创建授权')
  })

  it('clears local token and redirects even when logout request fails', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: {
          role: 'admin',
        },
      },
    })
    vi.mocked(http.post).mockRejectedValue(new Error('network error'))
    sessionStorage.setItem('login_token', 'token-a')

    const wrapper = mountMainPanel()
    await flushPromises()
    await wrapper.findAll('button').find((button) => button.text() === '退出登录')!.trigger('click')
    await flushPromises()

    expect(http.post).toHaveBeenCalledWith('/log_out')
    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(mocks.routerPush).toHaveBeenCalledWith('/')
  })

  it('ignores repeated logout while request is pending', async () => {
    vi.mocked(http.get).mockResolvedValue({
      data: {
        data: {
          role: 'admin',
        },
      },
    })
    let rejectLogout!: (reason?: any) => void
    vi.mocked(http.post).mockReturnValue(new Promise((_, reject) => {
      rejectLogout = reject
    }))
    sessionStorage.setItem('login_token', 'token-a')

    const wrapper = mountMainPanel()
    await flushPromises()
    const logoutButton = wrapper.findAll('button').find((button) => button.text() === '退出登录')!
    await logoutButton.trigger('click')
    await logoutButton.trigger('click')

    expect(http.post).toHaveBeenCalledTimes(1)

    rejectLogout(new Error('network error'))
    await flushPromises()

    expect(sessionStorage.getItem('login_token')).toBeNull()
    expect(mocks.routerPush).toHaveBeenCalledWith('/')
  })
})
